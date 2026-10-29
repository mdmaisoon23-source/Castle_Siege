// Silent audio backend for builds without FMOD (e.g. quick Mac testing).
#include "Engine.h"

namespace cs {
namespace {
class NullAudio final : public IAudio {
public:
    bool Init(const std::string&) override { return true; }
    void LoadBankAsync(const std::string&) override {}
    bool IsBankLoaded(const std::string&) override { return false; }
    void PlayEvent(const std::string&) override {}
    void Update() override {}
    void Shutdown() override {}
};
} // namespace

std::unique_ptr<IAudio> CreateNullAudio() { return std::make_unique<NullAudio>(); }

} // namespace cs
