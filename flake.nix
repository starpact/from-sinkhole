{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, flake-utils }: {
    devShell.aarch64-darwin =
      let
        pkgs = import nixpkgs { system = "aarch64-darwin"; };
      in
      pkgs.mkShell.override { stdenv = pkgs.llvmPackages_latest.stdenv; } {
        nativeBuildInputs = [ pkgs.clang-tools ];
        buildInputs = with pkgs; [ llvmPackages_latest.llvm ];
        shellHook = "$CC -v";
      };

    devShell.x86-64-linux =
      let
        pkgs = import nixpkgs { system = "aarch64-darwin"; };
      in
      pkgs.mkShell {
        shellHook = "$CC -v";
      };
  };
}
