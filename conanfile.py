from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class KqTunnelConan(ConanFile):
    name = "kq-tunnel"
    version = "0.1"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("asio/[>=1.30]")
        self.requires("spdlog/[>=1.14]")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
