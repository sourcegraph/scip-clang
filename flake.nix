{
  inputs = {
    flake-utils = { url = "github:numtide/flake-utils"; };
    nixpkgs     = { url = "github:NixOS/nixpkgs/nixos-24.05"; };
  };

  outputs = { self, flake-utils, nixpkgs }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        bazelisk-wrapper = pkgs.writeShellScriptBin "bazelisk" ''
          exec ${fhs}/bin/bazel-fhs -c "bazelisk $*"
        '';
        fhs              = pkgs.buildFHSUserEnv({
          name       = "bazel-fhs";
          targetPkgs = pkgs: with pkgs; [
            bash
            bazelisk
            coreutils
            glibc.dev
            libxcrypt-legacy
            python3
            stdenv.cc.cc.lib
            zlib
          ];
        });
        pkgs         = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell({ packages = [ bazelisk-wrapper ]; });
      }
    );
}
