#include "plugin.hpp"
#include "voicebank/Voicebank.hpp"

#include <osdialog.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>

namespace vocalrack {

struct SingerPlateModule : rack::engine::Module {
    std::string singerId = "builtin:adachi-rei";
    std::string externalSingerPath;
    int fitMode = 0;
    bool showName = true;
    bool externalSingerNeedsRelink = false;
    SingerPlateModule() { config(0, 0, 0, 0); }
    json_t* dataToJson() override {
        json_t* root = json_object(); json_object_set_new(root, "singerId", json_string(singerId.c_str()));
        json_object_set_new(root, "externalSingerPath", json_string(externalSingerPath.c_str()));
        json_object_set_new(root, "fitMode", json_integer(fitMode)); json_object_set_new(root, "showName", json_boolean(showName)); return root;
    }
    void dataFromJson(json_t* root) override {
        if (auto* j = json_object_get(root, "singerId"); json_is_string(j)) singerId = json_string_value(j);
        if (auto* j = json_object_get(root, "externalSingerPath"); json_is_string(j)) externalSingerPath = json_string_value(j);
        externalSingerNeedsRelink = singerId != "builtin:adachi-rei";
        if (auto* j = json_object_get(root, "fitMode"); json_is_integer(j)) fitMode = static_cast<int>(json_integer_value(j));
        if (auto* j = json_object_get(root, "showName"); json_is_boolean(j)) showName = json_is_true(j);
    }
    std::filesystem::path root() const {
        if (singerId == "builtin:adachi-rei") return asset::plugin(pluginInstance, "res/singers/adachi-rei");
        return externalSingerNeedsRelink ? std::filesystem::path{} : std::filesystem::path(externalSingerPath);
    }
};

struct SingerPortrait : rack::widget::OpaqueWidget {
    SingerPlateModule* module = nullptr;
    std::string loadedKey, singerName = "足立レイ";
    std::filesystem::path imagePath;
    std::future<Voicebank> loadFuture;
    std::string loadingKey;
    void step() override {
        if (module) {
            const std::string key = module->singerId + "|" + module->root().string();
            if (module->root().empty()) {
                loadedKey = key;
                singerName = "Singer needs relink";
                imagePath.clear();
            } else if (loadFuture.valid() && loadFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto bank = loadFuture.get();
                if (loadingKey == key) {
                    loadedKey = key;
                    singerName = bank.character.name.empty() ? "Singer missing" : bank.character.name;
                    imagePath = bank.character.imagePath;
                }
            } else if (!loadFuture.valid() && key != loadedKey) {
                loadingKey = key;
                const auto root = module->root();
                const auto id = module->singerId;
                loadFuture = std::async(std::launch::async, [root, id] { return Voicebank::load(root, id); });
            }
        } Widget::step();
    }
    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg; nvgBeginPath(vg); nvgRect(vg, 0, 0, box.size.x, box.size.y); nvgFillColor(vg, nvgRGB(17, 20, 27)); nvgFill(vg);
        bool drawn = false;
        if (!imagePath.empty()) {
            auto image = APP->window->loadImage(imagePath.string());
            if (image && image->handle >= 0) {
                int iw = 1, ih = 1; nvgImageSize(vg, image->handle, &iw, &ih); const float sx = box.size.x / iw, sy = box.size.y / ih;
                const float scale = module && module->fitMode == 1 ? std::max(sx, sy) : std::min(sx, sy);
                const float w = iw * scale, h = ih * scale, x = (box.size.x - w) / 2, y = (box.size.y - h) / 2;
                // NanoVG image patterns repeat outside their source rectangle. Draw only
                // the single destination rectangle and clip it to the portrait viewport,
                // otherwise Fit mode wraps the feet/head into the letterbox margins.
                nvgSave(vg); nvgIntersectScissor(vg, 0, 0, box.size.x, box.size.y);
                nvgBeginPath(vg); nvgRect(vg, x, y, w, h); nvgFillPaint(vg, nvgImagePattern(vg, x, y, w, h, 0, image->handle, 1.f)); nvgFill(vg);
                nvgRestore(vg); drawn = true;
            }
        }
        if (!drawn) {
            nvgBeginPath(vg); nvgCircle(vg, box.size.x / 2, box.size.y * 0.42f, box.size.x * 0.2f); nvgFillColor(vg, nvgRGB(70, 78, 92)); nvgFill(vg);
            nvgBeginPath(vg); nvgRoundedRect(vg, box.size.x * 0.2f, box.size.y * 0.58f, box.size.x * 0.6f, box.size.y * 0.3f, 12); nvgFill(vg);
        }
        if (module && module->showName) {
            nvgBeginPath(vg); nvgRect(vg, 0, box.size.y - 34, box.size.x, 34); nvgFillColor(vg, nvgRGBA(4, 7, 12, 205)); nvgFill(vg);
            nvgFontFaceId(vg, APP->window->uiFont->handle); nvgFontSize(vg, 15); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(250, 230, 239)); nvgText(vg, box.size.x / 2, box.size.y - 17, singerName.c_str(), nullptr);
        }
    }
};

struct SingerPlateWidget : rack::app::ModuleWidget {
    SingerPlateWidget(SingerPlateModule* module) {
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/SingerPlate.svg")));
        auto* portrait = new SingerPortrait; portrait->module = module; portrait->box = {{8, 32}, {box.size.x - 16, box.size.y - 56}}; addChild(portrait);
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0))); addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }
    void appendContextMenu(Menu* menu) override {
        auto* module = getModule<SingerPlateModule>(); if (!module) return; menu->addChild(new MenuSeparator); menu->addChild(createMenuLabel("Singer Plate"));
        menu->addChild(createMenuItem("Bundled Adachi Rei", CHECKMARK(module->singerId == "builtin:adachi-rei"), [module]{ module->singerId = "builtin:adachi-rei"; module->externalSingerPath.clear(); module->externalSingerNeedsRelink = false; }));
        menu->addChild(createMenuItem("Select voicebank folder...", "", [module]{ char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr); if (path) { module->externalSingerPath = path; module->singerId = "external:" + module->externalSingerPath; module->externalSingerNeedsRelink = false; std::free(path); } }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createCheckMenuItem("Fit", "", [module]{ return module->fitMode == 0; }, [module]{ module->fitMode = 0; }));
        menu->addChild(createCheckMenuItem("Fill / Crop", "", [module]{ return module->fitMode == 1; }, [module]{ module->fitMode = 1; }));
        menu->addChild(createBoolPtrMenuItem("Show singer name", "", &module->showName));
    }
};

}  // namespace vocalrack

Model* modelSingerPlate = createModel<vocalrack::SingerPlateModule, vocalrack::SingerPlateWidget>("SingerPlate");
