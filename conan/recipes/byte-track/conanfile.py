from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy

import os

class ByteTrack(ConanFile):
    name = "byte-track"
    version = "1.0.0"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": False}
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "windows_compat/*"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h", src=os.path.join(self.source_folder, "include/ByteTrack"), dst=os.path.join(self.package_folder, "include/ByteTrack"))
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "byte_track")
        self.cpp_info.set_property("cmake_target_name", "byte_track::byte_track")
        self.cpp_info.set_property("pkg_config_name", "byte_track")
        self.cpp_info.components["byte_tracklib"].libs = ["ByteTrack"]
        if self.settings.os != "Windows":
            self.cpp_info.components["byte_tracklib"].requires = ["eigen::eigen", "libuuid::libuuid"]
        else:
            self.cpp_info.components["byte_tracklib"].requires = ["eigen::eigen"]

    def requirements(self):
        self.requires("eigen/3.4.0", transitive_headers=True, transitive_libs=True)
        if self.settings.os != "Windows":
            self.requires("libuuid/1.0.3", transitive_headers=True, transitive_libs=True)

    def build_requirements(self):
        self.tool_requires("cmake/[>=4.0]")
