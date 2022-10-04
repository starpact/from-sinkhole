{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, flake-utils, nixpkgs }:
    flake-utils.lib.eachDefaultSystem (system:
      {
        devShell = (import nixpkgs { inherit system; }).mkShell {
          shellHook = "$CC -v";
        };
      }
    );
}
