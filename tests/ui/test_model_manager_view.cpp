// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

#include "Core/AI/ModelTypes.hpp"
#include "UI/Settings/ModelManagerComponent.hpp"
#include "UI/Theme.hpp"

using namespace zyron;

namespace {

class MockModelManager : public core::IModelManager {
 public:
  std::vector<core::ModelMetadata> models;
  std::string importedId;
  std::string removedId;

  [[nodiscard]] std::vector<core::ModelMetadata> availableModels() const override {
    return models;
  }

  [[nodiscard]] core::ModelInstallStatus status(const std::string& modelId) const override {
    for (const auto& m : models) {
      if (m.id == modelId) return m.status;
    }
    return core::ModelInstallStatus::NotInstalled;
  }

  [[nodiscard]] std::string modelPath(const std::string& modelId) const override {
    for (const auto& m : models) {
      if (m.id == modelId) return m.localFilePath;
    }
    return {};
  }

  bool importModel(const std::string& modelId, const std::string& /*sourcePath*/) override {
    importedId = modelId;
    for (auto& m : models) {
      if (m.id == modelId) {
        m.status = core::ModelInstallStatus::Ready;
        return true;
      }
    }
    return false;
  }

  bool removeModel(const std::string& modelId) override {
    removedId = modelId;
    for (auto& m : models) {
      if (m.id == modelId) {
        m.status = core::ModelInstallStatus::NotInstalled;
        return true;
      }
    }
    return false;
  }

  void refresh() override {}
};

}  // namespace

TEST_CASE("ModelManagerComponent: table and model management interactions", "[ui][models]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  auto mock = std::make_shared<MockModelManager>();

  core::ModelMetadata m1;
  m1.id = "htdemucs-onnx";
  m1.name = "HT-Demucs 4-Stem";
  m1.version = "4.0.0";
  m1.task = core::ModelTask::StemSeparation;
  m1.license = "MIT";
  m1.isPermissive = true;
  m1.sizeBytes = 331818228;
  m1.status = core::ModelInstallStatus::Ready;
  m1.supportedBackends = "CUDA, CPU";
  mock->models.push_back(m1);

  core::ModelMetadata m2;
  m2.id = "mert-v2-fullsong";
  m2.name = "MERT v2 Full-Song";
  m2.version = "2.0.0";
  m2.task = core::ModelTask::StructureSegmentation;
  m2.license = "CC BY-NC 4.0";
  m2.isPermissive = false;  // NC non-permissive warning badge
  m2.sizeBytes = 1200000000;
  m2.status = core::ModelInstallStatus::NotInstalled;
  m2.supportedBackends = "CUDA, CPU";
  mock->models.push_back(m2);

  ui::ModelManagerComponent comp(mock);
  comp.setSize(800, 400);

  SECTION("Populates model table with rows") {
    CHECK(comp.getNumRows() == 2);
    CHECK(comp.models().size() == 2);
  }

  SECTION("Paints cells without assertion errors") {
    juce::Image testImg(juce::Image::ARGB, 800, 400, true);
    juce::Graphics g(testImg);

    for (int col = 1; col <= 7; ++col) {
      comp.paintCell(g, 0, col, 100, 24, false);
      comp.paintCell(g, 1, col, 100, 24, true);
    }
    comp.paintRowBackground(g, 0, 800, 24, false);
    comp.paintRowBackground(g, 1, 800, 24, true);
  }

  SECTION("Triggers import callback on cell double click") {
    std::string requestedId;
    comp.onImportRequested = [&](const std::string& id) {
      requestedId = id;
    };

    juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),
                       juce::Point<float>{0.0F, 0.0F},
                       juce::ModifierKeys{},
                       0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                       &comp, &comp,
                       juce::Time{},
                       juce::Point<float>{0.0F, 0.0F},
                       juce::Time{},
                       2, false);
    comp.cellDoubleClicked(0, 1, e);
    CHECK(requestedId == "htdemucs-onnx");

    comp.cellDoubleClicked(1, 1, e);
    CHECK(requestedId == "mert-v2-fullsong");
  }
}
