using System.Text;
using NAudio.Wave;
using OpenUtau.Classic;
using OpenUtau.Core;
using OpenUtau.Core.Format;
using OpenUtau.Core.SignalChain;
using OpenUtau.Core.Ustx;

if (args.Length != 3) {
    Console.Error.WriteLine("Usage: OpenUtauCompare INPUT.ustx OUTPUT.wav WORLDLINE-R|CLASSIC");
    return 2;
}

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
DocManager.Inst.PostOnUIThread = _ => { }; // Headless comparison has no progress UI.
DocManager.Inst.Initialize(Thread.CurrentThread, TaskScheduler.Default);
SingerManager.Inst.Initialize();
ToolsManager.Inst.Initialize();

var input = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
var renderer = args[2].ToUpperInvariant();
if (renderer != "WORLDLINE-R" && renderer != "CLASSIC") {
    Console.Error.WriteLine($"Unsupported renderer '{args[2]}'; expected WORLDLINE-R or CLASSIC.");
    return 2;
}
var project = Ustx.Load(input);
foreach (var track in project.tracks) {
    track.RendererSettings.renderer = renderer;
    // Explicit fresh-install defaults keep the Classic comparison reproducible.
    track.RendererSettings.resampler = renderer == "CLASSIC" ? "worldline" : null;
    track.RendererSettings.wavtool = renderer == "CLASSIC" ? "convergence" : null;
}
DocManager.Inst.ExecuteCmd(new LoadProjectNotification(project));
// Dictionary-backed phonemizers normally initialize asynchronously and then
// post ValidateProject/PreRender notifications through the desktop UI. This
// headless harness has no UI dispatcher, so use OpenUtau's
// synchronous testing path and reattach the already resolved singer before
// the one authoritative full validation below.
foreach (var track in project.tracks) {
    var singer = track.Singer;
    track.Phonemizer.Testing = true;
    track.Phonemizer.SetSinger(null);
    track.Phonemizer.SetSinger(singer);
}
project.ValidateFull();

// Headless OpenUtau schedules phonemization on its worker pool. Cold Docker
// runs and arm64 emulation can legitimately take several minutes for a large
// coverage shard, especially while the host is building another target. A
// partially rebuilt phrase list is never safe to render, so use the same
// five-minute ceiling as OpenUtau's Classic subprocesses and continue to fail
// closed unless the strict stable state below is reached.
var deadline = DateTime.UtcNow.AddMinutes(5);
var parts = project.parts.OfType<UVoicePart>().ToArray();
// PhonemesUpToDate is set before OpenUtau finishes validating phonemes and
// rebuilding renderPhrases. Waiting only on that flag races the headless
// RenderEngine and occasionally hands it a partially rebuilt phrase list.
// The list is also cleared/repopulated outside UVoicePart's lock in pinned
// OpenUtau, so require a complete phone array and a stable 500 ms window.
DateTime? readySince = null;
string? readySignature = null;
DateTime? allInvalidSince = null;
while (DateTime.UtcNow < deadline) {
    var ready = parts.Length > 0 && parts.All(part =>
        part.PhonemesUpToDate && part.phonemes.Count > 0 && part.renderPhrases.Count > 0 &&
        part.renderPhrases.All(phrase => phrase != null && phrase.phones != null && phrase.phones.Length > 0));
    var signature = ready ? string.Join("|", parts.Select(part =>
        $"{part.phonemesRevision}:{part.phonemes.Count}:{part.renderPhrases.Count}:" +
        string.Join(",", part.renderPhrases.Select(phrase => phrase.phones.Length)))) : null;
    if (!ready || signature != readySignature) {
        readySince = null;
        readySignature = signature;
    } else if (readySince == null) {
        readySince = DateTime.UtcNow;
    } else if (DateTime.UtcNow - readySince.Value >= TimeSpan.FromMilliseconds(500)) {
        break;
    }
    var allInvalid = parts.Length > 0 && parts.All(part =>
        part.PhonemesUpToDate && part.phonemes.Count > 0 && part.phonemes.All(phone => phone.Error));
    if (!allInvalid) {
        allInvalidSince = null;
    } else if (allInvalidSince == null) {
        allInvalidSince = DateTime.UtcNow;
    } else if (DateTime.UtcNow - allInvalidSince.Value >= TimeSpan.FromSeconds(2)) {
        break;
    }
    await Task.Delay(25);
}
if (parts.Length == 0 || parts.Any(part =>
        !part.PhonemesUpToDate || part.phonemes.Count == 0 || part.renderPhrases.Count == 0 ||
        part.renderPhrases.Any(phrase => phrase == null || phrase.phones == null || phrase.phones.Length == 0)) ||
    readySince == null || DateTime.UtcNow - readySince.Value < TimeSpan.FromMilliseconds(500)) {
    Console.Error.WriteLine("OpenUtau phonemization did not complete.");
    foreach (var track in project.tracks) {
        Console.Error.WriteLine($"  track={track.TrackName} singer={track.Singer?.Id ?? "<null>"} " +
            $"found={track.Singer?.Found} loaded={track.Singer?.Loaded} phonemizer={track.Phonemizer?.GetType().FullName ?? "<null>"}");
    }
    foreach (var part in parts) {
        Console.Error.WriteLine($"  part={part.name} notes={part.notes.Count} upToDate={part.PhonemesUpToDate} " +
            $"phonemes={part.phonemes.Count} phrases={part.renderPhrases.Count} revision={part.phonemesRevision}");
        foreach (var phone in part.phonemes) {
            Console.Error.WriteLine(
                $"    phoneme={phone.phoneme} mapped={phone.phonemeMapped} error={phone.Error} " +
                $"tick={phone.position} parent={phone.Parent?.lyric ?? "<none>"}");
        }
    }
    return 3;
}
if (project.tracks.Count != 1 || project.tracks[0].Singer == null || !project.tracks[0].Singer.Found) {
    var requestedSinger = project.tracks.Count == 1 ? project.tracks[0].Singer?.Id ?? "<unresolved>" : "<invalid track count>";
    Console.Error.WriteLine($"OpenUtau could not attach the requested singer '{requestedSinger}'.");
    return 4;
}

Directory.CreateDirectory(Path.GetDirectoryName(output)!);
var engineType = typeof(PlaybackManager).Assembly.GetType("OpenUtau.Core.Render.RenderEngine")
    ?? throw new InvalidOperationException("Pinned OpenUtau RenderEngine type was not found.");
var engine = Activator.CreateInstance(engineType, project, 0, -1, -1)
    ?? throw new InvalidOperationException("Pinned OpenUtau RenderEngine could not be created.");
var renderMethod = engineType.GetMethods()
    .Single(method => method.Name == "RenderMixdown" && method.GetParameters().Length == 3);
object?[] renderArgs = { DocManager.Inst.MainScheduler, null, true };
var renderResult = renderMethod.Invoke(engine, renderArgs)
    ?? throw new InvalidOperationException("Pinned OpenUtau RenderEngine returned no mix.");
var projectMix = (ISignalSource)(renderResult.GetType().GetProperty("Item1")?.GetValue(renderResult)
    ?? throw new InvalidOperationException("Pinned OpenUtau RenderEngine returned no signal source."));
WaveFileWriter.CreateWaveFile16(output, new HeadlessExportAdapter(projectMix));
if (!File.Exists(output) || new FileInfo(output).Length < 1024) {
    Console.Error.WriteLine("OpenUtau did not produce a usable WAV.");
    return 5;
}

Console.WriteLine($"OpenUtau singer: {project.tracks[0].Singer.Name} ({project.tracks[0].Singer.Id})");
Console.WriteLine($"OpenUtau renderer: {project.tracks[0].RendererSettings.renderer}");
Console.WriteLine("OpenUtau phonemes: " + string.Join(" ", parts.SelectMany(part => part.phonemes).Select(phone => phone.phoneme)));
foreach (var phone in parts.SelectMany(part => part.phonemes)) {
    Console.WriteLine(
        $"  phoneme={phone.phoneme} mapped={phone.phonemeMapped} tick={phone.position} durationTick={phone.Duration} " +
        $"positionMs={phone.PositionMs:F3} durationMs={phone.DurationMs:F3} preutterMs={phone.preutter:F3} " +
        $"overlapMs={phone.overlap:F3} tailIntrudeMs={phone.tailIntrude:F3} tailOverlapMs={phone.tailOverlap:F3} " +
        "envelope=" + string.Join(";", phone.envelope.data.Select(point => $"({point.X:F3},{point.Y:F3})")));
}
Console.WriteLine($"OpenUtau output: {output}");
return 0;

sealed class HeadlessExportAdapter : ISampleProvider {
    readonly WaveFormat waveFormat = WaveFormat.CreateIeeeFloatWaveFormat(44100, 2);
    readonly ISignalSource source;
    int position;

    public HeadlessExportAdapter(ISignalSource source) => this.source = source;
    public WaveFormat WaveFormat => waveFormat;

    public int Read(float[] buffer, int offset, int count) {
        Array.Clear(buffer, offset, count);
        if (!source.IsReady(position, count)) {
            throw new InvalidOperationException("OpenUtau mix source was not ready during export.");
        }
        var next = source.Mix(position, buffer, offset, count);
        var read = Math.Max(0, next - position);
        position = next;
        return read;
    }
}
