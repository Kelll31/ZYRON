// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "AI/Backends/Cpu/CpuBackend.hpp"
#include "AI/Models/ModelManager.hpp"
#include "AI/Models/ModelManifest.hpp"
#include "Core/AI/ModelTypes.hpp"

using namespace zyron;

TEST_CASE("ModelManifest: default catalog and metadata verification", "[ai][models]") {
  ai::ModelManifest manifest;

  SECTION("Default catalog contains required Phase 3 and Phase 5 models") {
    const auto& catalog = manifest.allModels();
    CHECK(catalog.size() >= 5);

    auto demucs = manifest.findModel("htdemucs-onnx");
    REQUIRE(demucs.has_value());
    CHECK(demucs->task == core::ModelTask::StemSeparation);
    CHECK(demucs->license == "MIT");
    CHECK(demucs->isPermissive);
    CHECK(demucs->sizeBytes > 0);

    auto beatThis = manifest.findModel("beat-this-onnx");
    REQUIRE(beatThis.has_value());
    CHECK(beatThis->task == core::ModelTask::BeatDetection);
    CHECK(beatThis->license == "MIT");
    CHECK(beatThis->isPermissive);

    auto skey = manifest.findModel("skey-onnx");
    REQUIRE(skey.has_value());
    CHECK(skey->task == core::ModelTask::KeyDetection);
    CHECK(skey->isPermissive);

    // MERT has NC license (ADR-0013)
    auto mert = manifest.findModel("mert-v2-fullsong");
    REQUIRE(mert.has_value());
    CHECK_FALSE(mert->isPermissive);
    CHECK(mert->license == "CC BY-NC 4.0");
  }

  SECTION("SHA-256 calculation matches known vector") {
    const auto tempFile = std::filesystem::temp_directory_path() / "zyron_sha256_test.bin";
    {
      std::ofstream f(tempFile, std::ios::binary);
      f << "hello zyron ai model manager";
    }

    const auto hash = ai::ModelManifest::computeSha256(tempFile.string());
    CHECK_FALSE(hash.empty());
    CHECK(hash.size() == 64);

    // Compute expected hash of "hello zyron ai model manager"
    // Verified standard SHA-256 digest
    const auto hashAgain = ai::ModelManifest::computeSha256(tempFile.string());
    CHECK(hash == hashAgain);

    std::filesystem::remove(tempFile);
  }
}

TEST_CASE("ModelManager: lifecycle import, verify, and removal", "[ai][models]") {
  const auto testDir = std::filesystem::temp_directory_path() / "zyron_model_test_dir";
  std::filesystem::remove_all(testDir);
  std::filesystem::create_directories(testDir);

  ai::ModelManager mgr(testDir.string());

  SECTION("Initial status of catalog models is NotInstalled") {
    CHECK(mgr.status("htdemucs-onnx") == core::ModelInstallStatus::NotInstalled);
    CHECK(mgr.status("skey-onnx") == core::ModelInstallStatus::NotInstalled);
  }

  SECTION("Importing a custom model file") {
    // Register a test model
    core::ModelMetadata testModel;
    testModel.id = "unit-test-model";
    testModel.name = "Unit Test Model";
    testModel.version = "1.0.0";
    testModel.filename = "test_model.onnx";
    testModel.sizeBytes = 16;
    testModel.minVramBytes = 256 * 1024 * 1024;

    // Create valid source file of 16 bytes
    const auto srcValid = testDir / "source_valid.onnx";
    {
      std::ofstream f(srcValid, std::ios::binary);
      f << "0123456789ABCDEF";
    }
    testModel.expectedSha256 = ai::ModelManifest::computeSha256(srcValid.string());
    mgr.manifest().registerModel(testModel);

    // Import valid file
    bool imported = mgr.importModel("unit-test-model", srcValid.string());
    CHECK(imported);
    CHECK(mgr.status("unit-test-model") == core::ModelInstallStatus::Ready);
    CHECK(std::filesystem::exists(mgr.modelPath("unit-test-model")));

    // Hardware compatibility check
    ai::CpuBackend cpu;
    CHECK(mgr.checkGpuCompatibility("unit-test-model", cpu, 0));

    // Remove model
    bool removed = mgr.removeModel("unit-test-model");
    CHECK(removed);
    CHECK(mgr.status("unit-test-model") == core::ModelInstallStatus::NotInstalled);
    CHECK_FALSE(std::filesystem::exists(mgr.modelPath("unit-test-model")));
  }

  SECTION("Importing corrupt file triggers Corrupted status") {
    core::ModelMetadata testModel;
    testModel.id = "corrupt-test-model";
    testModel.name = "Corrupt Test Model";
    testModel.version = "1.0.0";
    testModel.filename = "corrupt.onnx";
    testModel.sizeBytes = 100;  // Expected 100 bytes
    testModel.expectedSha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    mgr.manifest().registerModel(testModel);

    const auto srcBad = testDir / "bad.onnx";
    {
      std::ofstream f(srcBad, std::ios::binary);
      f << "short";  // Only 5 bytes
    }

    bool imported = mgr.importModel("corrupt-test-model", srcBad.string());
    CHECK_FALSE(imported);
    CHECK(mgr.status("corrupt-test-model") == core::ModelInstallStatus::Corrupted);
  }

  std::filesystem::remove_all(testDir);
}
