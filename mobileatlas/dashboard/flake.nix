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
      reqName =
        req:
        builtins.elemAt (builtins.match "^([A-Za-z0-9][A-Za-z0-9._-]*[A-Za-z0-9]|[A-Za-z0-9]).*$" req) 0;
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
            propagatedBuildInputs = map (d: py-pkgs.${reqName d}) pyproject.project.dependencies;
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
                    p.uvicorn
                  ]
                  ++ map (d: p.${reqName d}) pyproject.project.dependencies
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

        apps = {
          default = {
            type = "app";
            program = "${self.packages.${system}.default}/bin/moat-dashboard";
          };
        };
      }
    );
}
