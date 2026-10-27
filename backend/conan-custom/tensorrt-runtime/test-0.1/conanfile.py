from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy

import os

class TensorRTRuntimeConan(ConanFile):
    name = "tensorrt-runtime"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "with_opencv": [True, False]}
    default_options = {"shared": True, "with_opencv": True}
    exports_sources = "CMakeLists.txt", "cmake/*", "project/*"

    def generate(self):
        # Generate CMake toolchain and dependencies
        tc = CMakeToolchain(self)
        if self.options.with_opencv:
            tc.variables["With_OpenCV"] = True

        if self.options.shared:
            tc.variables["Shared"] = True
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    # Package the built artifacts
    def package(self):
        copy(self, "*.h", src=os.path.join(self.source_folder, "project/tensorrt_runtime/include"), dst=os.path.join(self.package_folder, "include/tensorrt_runtime/include"))
        copy(self, "*.h", src=os.path.join(self.source_folder, "project/engine/include"), dst=os.path.join(self.package_folder, "include/engine/include"))
        if self.options.with_opencv:
            copy(self, "*.h", src=os.path.join(self.source_folder, "project/float_image_tensor/include"), dst=os.path.join(self.package_folder, "include/float_image_tensor/include"))
        copy(self, "*.h", src=os.path.join(self.source_folder, "project/logger/include"), dst=os.path.join(self.package_folder, "include/logger/include"))
        copy(self, "*.h", src=os.path.join(self.source_folder, "project/tensor/include"), dst=os.path.join(self.package_folder, "include/tensor/include"))
        copy(self, "*.h", src=os.path.join(self.source_folder, "project/output_tensor/include"), dst=os.path.join(self.package_folder, "include/output_tensor/include"))
        if self.options.shared:
            copy(self, "*.so", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        else:
            copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)


    # Provide package information
    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "tensorrt_runtime")
        self.cpp_info.set_property("cmake_target_name", "tensorrt_runtime::tensorrt_runtime")
        self.cpp_info.set_property("pkg_config_name", "tensorrt_runtime")
        self.cpp_info.components["tensorrt_runtimelib"].libs = ["TensorRTRuntime"]
        self.cpp_info.components["tensorrt_runtimelib"].requires = ["log-factory::log-factory"]
        self.cpp_info.components["tensorrt_runtimelib"].requires.append("nlohmann_json::nlohmann_json")
        if self.options.with_opencv:
            self.cpp_info.components["tensorrt_runtimelib"].requires.append("opencv::opencv")

    def requirements(self):
        self.requires("log-factory/1.0.0-1-56934db0", transitive_headers=True, transitive_libs=True)
        self.requires("nlohmann_json/3.11.3")
        if self.options.with_opencv:
            self.requires("opencv/4.12.0")

    def build_requirements(self):
        self.tool_requires("cmake/[>=4.0]")
