from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy
import os
import yaml
from pathlib import Path

class LogFactoryMonitoring(ConanFile):
    name = "log-factory"
    version = "1.0.0"
    package_type = "header-library"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "cmake/*", "project/*", "build_config.yaml"

    def build_config(self):
        config_path = Path(self.recipe_folder) / "build_config.yaml"
        if config_path.exists():
            with open(config_path) as f:
                return yaml.safe_load(f)
        return {}

    def generate(self):
        config_path = Path(self.recipe_folder) / "build_config.yaml"
        if config_path.exists():
            self.output.info(f"Loading build config from: {config_path}")
            with open(config_path) as f:
                config = yaml.safe_load(f)
        else:
            self.output.info("No build_config.yaml found. Using default values.")
            config = {}

        build_example = config.get("build_example", False)

        self.env_info.BUILD_EXAMPLE = "ON" if build_example else "OFF"

        tc = CMakeToolchain(self)
        tc.cache_variables["BUILD_EXAMPLE"] = "ON" if build_example else "OFF"

        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "*.h",
             src=os.path.join(self.source_folder, "project/log_factory/include"),
             dst=os.path.join(self.package_folder, "include/base_component/monitoring/log"),
             keep_path=False)

    def requirements(self):
        self.requires("fmt/11.1.4", transitive_headers=True, transitive_libs=True)
        self.requires("log-entry/1.0.0", transitive_headers=True, transitive_libs=True)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "log_factory")
        self.cpp_info.set_property("cmake_target_name", "log_factory::log_factory")
        self.cpp_info.set_property("pkg_config_name", "log_factory")
        self.cpp_info.includedirs = ["include"]

    def build_requirements(self):
        self.tool_requires("cmake/[>=4.0]")
