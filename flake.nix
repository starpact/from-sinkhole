{
  description = "A Nix-flake-based C development environment";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, flake-utils, nixpkgs }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShell = (pkgs.mkShell.override { stdenv = pkgs.llvmPackages_latest.stdenv; }) {
          buildInputs = with pkgs; [
            lldb
            llvmPackages_latest.llvm
          ];
          nativeBuildInputs = with pkgs; [
            clang-tools
          ];

          shellHook = ''
            echo "`$CC -v`"
          '';
        };
      }
    );
}
