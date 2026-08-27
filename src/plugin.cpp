#include "plugin.hpp"
#include "phonemizer/Phonemizer.hpp"
#include "render/RenderService.hpp"

Plugin* pluginInstance = nullptr;

void init(Plugin* p) {
    pluginInstance = p;
    vocalrack::setEnglishDictionaryPath(
        rack::asset::plugin(p, "res/dictionaries/cmudict-0.7b.txt"));
    p->addModel(modelVocal);
    p->addModel(modelSingerPlate);
}
