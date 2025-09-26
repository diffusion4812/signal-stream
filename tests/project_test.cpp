#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include "projectenvironment.h"

ProjectManager mgr;
ProjectData pdata;
std::string err;

TEST(SignalStream, OpenProject) {
    LoadProjectFromFile("C:/Users/LOAR02/Downloads/test_project.json", pdata, err);
    mgr.LoadProject(pdata, false, err); // Load and start services

    EXPECT_EQ(mgr.GetProjectData().name, "Test Project");
    EXPECT_EQ(mgr.GetProjectData().streams.size(), 1);
}

TEST(SignalStream, StartServices) {
    mgr.StartAllServices(err);
}

TEST(SignalStream, GetRandomData) {
    auto svc = mgr.GetService("my random data");
    SampleHandle h; const uint8_t* data; size_t size; SampleMetadata meta;

    if (svc->AcquireSample(std::chrono::milliseconds(100), h, data, size, meta)) {
        // decode data
        svc->ReleaseSample(h);
    }
}