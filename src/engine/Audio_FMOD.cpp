// FMOD Studio audio backend. Requires the FMOD Studio API SDK (fmod.com, free for indie use under its license).
// Banks: Master.bank + Master.strings.bank (tiny, blocking), Title.bank (streamed .fsb, non-blocking).
#include <fmod_studio.hpp>
#include <fmod.hpp>
#include <cstdio>
#include <unordered_map>
#include "Engine.h"

namespace cs {
namespace {

class FmodAudio final : public IAudio {
public:
    bool Init(const std::string& bankDir) override {
        dir_ = bankDir;
        if (FMOD::Studio::System::create(&sys_) != FMOD_OK) return false;
        if (sys_->initialize(256, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr) != FMOD_OK) return false;

        // Master + strings first (blocking, small) so event paths resolve.
        FMOD::Studio::Bank *master = nullptr, *strings = nullptr;
        if (sys_->loadBankFile((dir_ + "/Master.bank").c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &master) != FMOD_OK) return false;
        sys_->loadBankFile((dir_ + "/Master.strings.bank").c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &strings);
        banks_["Master.bank"] = master;
        return true;
    }

    void LoadBankAsync(const std::string& file) override {
        FMOD::Studio::Bank* b = nullptr;
        // NONBLOCKING: returns immediately, loads on FMOD's thread; streamed sample data is not held in RAM.
        if (sys_->loadBankFile((dir_ + "/" + file).c_str(), FMOD_STUDIO_LOAD_BANK_NONBLOCKING, &b) == FMOD_OK)
            banks_[file] = b;
    }

    bool IsBankLoaded(const std::string& file) override {
        auto it = banks_.find(file);
        if (it == banks_.end() || !it->second) return false;
        FMOD_STUDIO_LOADING_STATE st;
        it->second->getLoadingState(&st);
        return st == FMOD_STUDIO_LOADING_STATE_LOADED;
    }

    void PlayEvent(const std::string& path) override {
        FMOD::Studio::EventDescription* desc = nullptr;
        if (sys_->getEvent(path.c_str(), &desc) != FMOD_OK) return;
        FMOD::Studio::EventInstance* inst = nullptr;
        desc->createInstance(&inst);
        inst->start();
        inst->release();  // auto-destroy when it stops
    }

    void Update() override { if (sys_) sys_->update(); }

    void Shutdown() override {
        if (sys_) { sys_->release(); sys_ = nullptr; }
        banks_.clear();
    }

private:
    std::string dir_;
    FMOD::Studio::System* sys_ = nullptr;
    std::unordered_map<std::string, FMOD::Studio::Bank*> banks_;
};

} // namespace

std::unique_ptr<IAudio> CreateFmodAudio() { return std::make_unique<FmodAudio>(); }

} // namespace cs
