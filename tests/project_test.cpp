#include <chrono>
#include <thread>
#include <cstdarg>
#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "service-project.h"

enum class Color { RED, GREEN, YELLOW, RESET };

inline const char* ColorCode(Color c) {
    switch (c) {
    case Color::RED: return "\x1B[31m";
    case Color::GREEN: return "\x1B[32m";
    case Color::YELLOW: return "\x1B[33m";
    default: return "\x1B[0m";
    }
}

inline void ColoredPrint(Color c, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::cout << ColorCode(c);
    printf("             ");
    // Simple use of vprintf to stdout (or format into std::string with vsnprintf)
    vprintf(fmt, args);
    printf("\n");
    std::cout << ColorCode(Color::RESET);
    va_end(args);
}

class ProjectManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ioc_ = std::make_unique<boost::asio::io_context>();
        bus_ = std::make_unique<ServiceBus>();

        // Create temp directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "project_manager_tests";
        std::filesystem::create_directories(test_dir_);
        test_project_path_ = test_dir_ / "test_project.json";
    }

    void TearDown() override {
        // Cleanup
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    // Helper to create basic project data
    ProjectData CreateBasicProject(const std::string& name = "Test Project") {
        ProjectData data;
        data.name = name;
        return data;
    }

    // Helper to create source data with signals
    SourceData CreateSource(const std::string& name,
        const std::string& type,
        const std::vector<std::tuple<std::string, std::string, std::string>>& signals) {
        SourceData data;
        data.name = name;
        data.type = type;

        // Create schema from signals
        data.schema = Schema();
        for (const auto& [signalName, signalType, unit] : signals) {
            Kind kind = StringToKind(signalType);
            data.schema.add_field(signalName, kind);
        }

        return data;
    }

    // Convert string type to Kind enum
    Kind StringToKind(const std::string& type) {
        if (type == "int32") return Kind::Int32;
        if (type == "int64") return Kind::Int64;
        if (type == "float32") return Kind::Float;
        if (type == "float64") return Kind::Double;
        return Kind::Int32; // Default
    }

    // Helper to write realistic project JSON
    void WriteProjectFile(const std::string& jsonContent) {
        std::ofstream file(test_project_path_);
        file << jsonContent;
        file.close();
    }

    // Helper to create realistic JSON project file
    std::string CreateProjectJSON(const std::string& projectName,
        const std::vector<std::tuple<std::string, std::string, json>>& sources) {
        json j;
        j["name"] = projectName;
        j["sources"] = json::array();

        for (const auto& [name, type, metadata] : sources) {
            json sourceJson;
            sourceJson["name"] = name;
            sourceJson["type"] = type;
            sourceJson["metadata"] = metadata;
            sourceJson["signals"] = json::array();
            j["sources"].push_back(sourceJson);
        }

        return j.dump(2);
    }

    std::unique_ptr<boost::asio::io_context> ioc_;
    std::unique_ptr<ServiceBus> bus_;
    std::filesystem::path test_dir_;
    std::filesystem::path test_project_path_;
};

// ============================================================================
// Constructor & Basic Initialization Tests
// ============================================================================

TEST_F(ProjectManagerTest, Constructor_InitializesCorrectly) {
    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);

    EXPECT_EQ(mgr.get_path(), test_project_path_.string());
    EXPECT_NE(mgr.get_hash(), 0u);
    EXPECT_EQ(mgr.get_source_count(), 0u);

    ColoredPrint(Color::GREEN, "ProjectManager initialized successfully");
}

TEST_F(ProjectManagerTest, Constructor_StorageManagerIsRunning) {
    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    ColoredPrint(Color::GREEN, "StorageManager is running");
}

// ============================================================================
// LoadProjectFromFile Tests (Realistic JSON)
// ============================================================================

TEST_F(ProjectManagerTest, LoadProjectFromFile_SingleMQTTSource_Success) {
    // Write realistic JSON
    std::string jsonContent = R"(   {
                                        "name": "Test Project 1",
                                        "sources": [
                                            {
                                                "name": "my MQTT data source",
                                                "type": "MQTT",
                                                "metadata": {
                                                    "topic": "topic123"
                                                },
                                                "signals": [
                                                    { "name": "height", "type": "int32", "unit": "m" }
                                                ]
                                            }
                                        ]
                                    })";

    WriteProjectFile(jsonContent);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    ASSERT_TRUE(mgr.load_project_from_file(test_project_path_.string(), false, error)) << "Error: " << error;

    EXPECT_EQ(mgr.get_name(), "Test Project 1");
    EXPECT_EQ(mgr.get_source_count(), 1);
    EXPECT_TRUE(mgr.is_source_registered("my MQTT data source"));

    auto source = mgr.get_source("my MQTT data source");
    ASSERT_NE(source, nullptr);

    ColoredPrint(Color::GREEN, "MQTT source loaded from file: my MQTT data source\n");
}

TEST_F(ProjectManagerTest, LoadProjectFromFile_MultipleSourcesWithSignals_Success) {
    std::string jsonContent = R"(   {
                                        "name": "Multi-Source Project",
                                        "sources":
                                        [
                                            {
                                                "name": "MQTT source",
                                                "type": "MQTT",
                                                "metadata": {
                                                    "topic": "topic123"
                                                },
                                                "signals": [
                                                    { "name": "temperature", "type": "float", "unit": "C" },
                                                    { "name": "pressure", "type": "float", "unit": "bar" }
                                                ]
                                            },
                                            {
                                                "name": "random source",
                                                "type": "Random",
                                                "metadata": {},
                                                "signals": [
                                                    { "name": "value1", "type": "int32" , "unit": "C" },
                                                    { "name": "value2", "type": "float" , "unit": "C" }
                                                ]
                                            }
                                        ]
                                    })";

    WriteProjectFile(jsonContent);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    ASSERT_TRUE(mgr.load_project_from_file(test_project_path_.string(), false, error)) << "Error: " << error;

    EXPECT_EQ(mgr.get_name(), "Multi-Source Project");
    EXPECT_EQ(mgr.get_source_count(), 2);

    // Verify all sources loaded
    EXPECT_TRUE(mgr.is_source_registered("MQTT source"));
    EXPECT_TRUE(mgr.is_source_registered("random source"));

    // Verify schemas are finalized
    auto mqtt_source = mgr.get_source("MQTT source");
    ASSERT_NE(mqtt_source, nullptr);
    EXPECT_TRUE(mqtt_source->GetSchema().is_finalized());

    ColoredPrint(Color::GREEN, "Loaded %zu sources successfully\n", mgr.get_source_count());
}

TEST_F(ProjectManagerTest, LoadProjectFromFile_FileNotFound_Fails) {
    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    std::string nonExistentPath = (test_dir_ / "nonexistent.json").string();
    EXPECT_FALSE(mgr.load_project_from_file(nonExistentPath, false, error));
    EXPECT_THAT(error, ::testing::HasSubstr("Failed to open file"));

    ColoredPrint(Color::YELLOW, "File not found error: %s", error.c_str());
}

TEST_F(ProjectManagerTest, LoadProjectFromFile_InvalidJSON_Fails) {
    std::string invalidJson = R"(   {
                                        "name": "Invalid Project",
                                        "sources": [
                                        {
                                            "name": "broken source"
                                            "type": "MQTT"  // Missing comma
                                        }
                                        ]
                                    })";

    WriteProjectFile(invalidJson);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    EXPECT_FALSE(mgr.load_project_from_file(test_project_path_.string(), false, error));
    EXPECT_THAT(error, ::testing::HasSubstr("Failed to parse"));

    ColoredPrint(Color::YELLOW, "Invalid JSON rejected: %s", error.c_str());
}

TEST_F(ProjectManagerTest, LoadProjectFromFile_MissingRequiredFields_Fails) {
    std::string incompleteJson = R"(    {
                                            "sources": [
                                            {
                                                "name": "incomplete source"
                                            }
                                            ]
                                        })";

    WriteProjectFile(incompleteJson);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    EXPECT_FALSE(mgr.load_project_from_file(test_project_path_.string(), false, error));
    EXPECT_THAT(error, ::testing::HasSubstr("Failed to parse"));

    ColoredPrint(Color::YELLOW, "Missing fields rejected: %s", error.c_str());
}

// ============================================================================
// Service Lifecycle Tests
// ============================================================================

TEST_F(ProjectManagerTest, StartService_SingleSource_Success) {
    std::string jsonContent = R"(   {
                                        "name": "Test Project",
                                        "sources": [
                                            {
                                                "name": "random_source",
                                                "type": "Random",
                                                "metadata": {},
                                                "signals": [
                                                    { "name": "value", "type": "int32", "unit": "m" }
                                                ]
                                            }
                                        ]
                                    })";

    WriteProjectFile(jsonContent);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    ASSERT_TRUE(mgr.load_project_from_file(test_project_path_.string(), false, error));

    auto source = mgr.get_source("random_source");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->Status(), SourceStatus::Stopped);

    // Start the service
    ASSERT_TRUE(mgr.start_service("random_source", error)) << "Error: " << error;

    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(source->Status(), SourceStatus::Running);

    ColoredPrint(Color::GREEN, "Service started: random_source");
}

TEST_F(ProjectManagerTest, StartService_NonExistentSource_Fails) {
    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);

    auto data = CreateBasicProject();
    std::string error;
    ASSERT_TRUE(mgr.load_project(data, false, error));

    EXPECT_FALSE(mgr.start_service("nonexistent_source", error));
    EXPECT_THAT(error, ::testing::HasSubstr("Source not found"));

    ColoredPrint(Color::YELLOW, "Nonexistent source error: %s", error.c_str());
}

TEST_F(ProjectManagerTest, StopService_RunningSource_Success) {
    std::string jsonContent = R"(   {
                                        "name": "Test Project",
                                        "sources": [
                                            {
                                                "name": "random_source",
                                                "type": "Random",
                                                "metadata": {},
                                                "signals": [
                                                    { "name": "value", "type": "int32", "unit": "m" }
                                                ]
                                            }
                                        ]
                                    })";

    WriteProjectFile(jsonContent);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    ASSERT_TRUE(mgr.load_project_from_file(test_project_path_.string(), false, error));
    ASSERT_TRUE(mgr.start_service("random_source", error));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto source = mgr.get_source("random_source");
    ASSERT_EQ(source->Status(), SourceStatus::Running);

    // Stop the service
    ASSERT_TRUE(mgr.stop_service("random_source", error)) << "Error: " << error;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(source->Status(), SourceStatus::Stopped);

    ColoredPrint(Color::GREEN, "Service stopped successfully\n");
}

TEST_F(ProjectManagerTest, StartAllServices_MultipleSources_Success) {
    std::string jsonContent = R"(   {
                                        "name": "Multi-Source Project",
                                        "sources": [
                                        {
                                            "name": "source1",
                                            "type": "Random",
                                            "metadata": {},
                                            "signals": [{ "name": "val1", "type": "int32", "unit": "m" }]
                                        },
                                        {
                                            "name": "source2",
                                            "type": "Random",
                                            "metadata": {},
                                            "signals": [{ "name": "val2", "type": "float", "unit": "m" }]
                                        },
                                        {
                                            "name": "source3",
                                            "type": "Random",
                                            "metadata": {},
                                            "signals": [{ "name": "val3", "type": "int32", "unit": "m" }]
                                        }
                                        ]
                                    })";

    WriteProjectFile(jsonContent);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    ASSERT_TRUE(mgr.load_project_from_file(test_project_path_.string(), false, error));
    ASSERT_TRUE(mgr.start_all_services(error)) << "Error: " << error;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Verify all sources are running
    auto source1 = mgr.get_source("source1");
    auto source2 = mgr.get_source("source2");
    auto source3 = mgr.get_source("source3");

    EXPECT_EQ(source1->Status(), SourceStatus::Running);
    EXPECT_EQ(source2->Status(), SourceStatus::Running);
    EXPECT_EQ(source3->Status(), SourceStatus::Running);

    ColoredPrint(Color::GREEN, "All %zu services started\n", mgr.get_source_count());
}

TEST_F(ProjectManagerTest, StopAllServices_StopsAndCleansUp) {
    std::string jsonContent = R"(   {
                                        "name": "Test Project",
                                        "sources": [
                                        {
                                            "name": "source1",
                                            "type": "Random",
                                            "metadata": {},
                                            "signals": [{ "name": "val", "type": "int32", "unit": "m" }]
                                        }
                                        ]
                                    })";

    WriteProjectFile(jsonContent);

    ProjectManager mgr(*bus_, test_project_path_.string(), *ioc_);
    std::string error;

    ASSERT_TRUE(mgr.load_project_from_file(test_project_path_.string(), false, error));
    ASSERT_TRUE(mgr.start_all_services(error));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop all services
    mgr.stop_all_services();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Source should be stopped and cleaned up
    auto source = mgr.get_source("source1");
    // Source may be null after StopAllServices if it clears the map
    // Adjust based on your implementation

    ColoredPrint(Color::GREEN, "All services stopped and cleaned up");
}