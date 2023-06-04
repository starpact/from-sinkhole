{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        stdenv = pkgs.llvmPackages_16.stdenv;
      in
      {
        devShells. default = pkgs.mkShell.override { inherit stdenv; } {
          nativeBuildInputs = [ pkgs.clang-tools_16 ];
          buildInputs = with pkgs; [ llvmPackages_16.llvm ];
          shellHook = "$CC -v";
        };
      }
    );
}
