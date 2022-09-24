{
  description = "A Nix-flake-based C development environment";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:NixOS/nixpkgs";
  };

  outputs = { self, flake-utils, nixpkgs }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShell = pkgs.mkShell {
          buildInputs = with pkgs; [ clang_14 lldb ];

          shellHook = ''
            echo "`${pkgs.clang_14}/bin/clang version`"
          '';
        };
      }
    );
}
