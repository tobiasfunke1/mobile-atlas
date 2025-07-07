{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    let
      pyproject = builtins.fromTOML (builtins.readFile ./pyproject.toml);
      py-deps =
        p: with p; [
          authlib
          fastapi
          httpx
          itsdangerous
          jinja2
          psycopg
          psycopg-pool
          pycountry
          pydantic-extra-types
          pydantic-settings
          pyjwt
          python-multipart
          redis
          uvicorn
          websockets
        ];
    in
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        python = pkgs.python313;
        py-pkgs = python.pkgs;
      in
      {
        packages = {
          default = py-pkgs.buildPythonPackage {
            pname = pyproject.project.name;
            inherit (pyproject.project) version;
            pyproject = true;

            src = ./.;

            build-system = [ py-pkgs.setuptools ];
            propagatedBuildInputs = py-deps py-pkgs;
          };

          dashboard-image = pkgs.dockerTools.streamLayeredImage {
            name = "moat-dashboard";
            tag = "latest";

            contents = [
              (python.withPackages (p: [
                self.packages.${system}.default
                p.gunicorn
              ]))
              pkgs.dockerTools.binSh
              pkgs.coreutils
              pkgs.cacert
            ];

            config = {
              WorkingDir = "/app";
              Entrypoint = [
                "gunicorn"
                "-k"
                "uvicorn.workers.UvicornWorker"
                "-b"
                "[::]:8000"
                "moat_dashboard.routes:app"
              ];
              Env = [
                "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
                "NIX_SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
                "PYTHONUNBUFFERED=1"
              ];
              ExposedPorts = {
                "8000" = { };
              };
            };
          };
        };

        devShells = {
          default =
            let
              defpkg = self.packages.${system}.default;
              editablePkg = py-pkgs.mkPythonEditablePackage {
                inherit (defpkg) pname version;
                root = "$EDITABLE_PKG_PATH/src";
                inherit (pyproject.project) scripts;
              };
            in
            pkgs.mkShellNoCC {
              packages = [
                (python.withPackages (
                  p:
                  [
                    editablePkg
                    p.pytest
                    p.gunicorn
                  ]
                  ++ py-deps p
                ))

                pkgs.black
                pkgs.isort
                pkgs.pyright
              ];

              shellHook = ''
                export EDITABLE_PKG_PATH="$PWD"
              '';
            };
        };

        apps =
          let
            freeze-nix-deps = pkgs.stdenv.mkDerivation {
              pname = "freeze-nix-deps";
              version = "0.0.1";

              phases = [ "buildPhase" ];

              buildInputs = [
                (python.withPackages (p: (py-deps p) ++ [ p.pip ]))
              ];

              buildPhase = ''
                echo '#!/bin/bash' >"$out"
                echo 'cat <<END' >>"$out"
                python -m pip --no-cache-dir freeze >>"$out"
                echo 'END' >>"$out"

                chmod 755 "$out"
              '';
            };
          in
          {
            default = {
              type = "app";
              program = "${self.packages.${system}.default}/bin/moat-dashboard";
            };
            freeze = {
              type = "app";
              program = "${freeze-nix-deps}";
            };
          };
      }
    );
}
