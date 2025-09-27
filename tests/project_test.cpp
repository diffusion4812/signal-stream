#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include "projectenvironment.h"

ProjectManager mgr;
ProjectData pdata;
Schema schema;
std::string err;

TEST(SignalStream, OpenProject) {
    LoadProjectFromFile("C:/Users/LOAR02/Downloads/test_project.json", pdata, err);
    mgr.LoadProject(pdata, false, err); // Load and start services

    EXPECT_EQ(mgr.GetProjectData().name, "Test Project");
    EXPECT_EQ(mgr.GetProjectData().streams.size(), 1);
}

TEST(SignalStream, StartServices) {
    bool started;
    started = mgr.StartAllServices(err);
    auto svc = mgr.GetService("my random data");
    svc->Stop();
    EXPECT_EQ(started, false);
}

TEST(SignalStream, AddSchema_FAIL) {
    bool schemaloaded = false;
    schema.add_field("field1", Kind::Int32);
    schema.add_field("field2", Kind::String);
    auto svc = mgr.GetService("my random data");
    if (svc) {
        schemaloaded = svc->SetupSchema(schema);
    }
    EXPECT_FALSE(schemaloaded);
}

TEST(SignalStream, AddSchema_PASS) {
    bool schemaloaded = false;
    schema.finalize(); // Must finalize before use
    auto svc = mgr.GetService("my random data");
    if (svc) {
        schemaloaded = svc->SetupSchema(schema);
    }
    EXPECT_TRUE(schemaloaded);
}

TEST(SignalStream, GetRandomData) {
    auto svc = mgr.GetService("my random data");
    SampleHandle h; const uint8_t* data; size_t size; SampleMetadata meta;

    if (svc->AcquireSample(std::chrono::milliseconds(100), h, data, size, meta)) {
        // decode data
        svc->ReleaseSample(h);
    }
}