{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        stdenv = pkgs.llvmPackages_15.stdenv;
      in
      {
        devShells. default = pkgs.mkShell.override { inherit stdenv; } {
          nativeBuildInputs = [ pkgs.clang-tools ];
          buildInputs = with pkgs; [ llvmPackages_15.llvm ];
          shellHook = "$CC -v";
        };
      }
    );
}
