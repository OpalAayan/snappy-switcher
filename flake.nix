{
  description = "Snappy Switcher - A fast, keyboard-driven window switcher for Wayland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "snappy-switcher";
          version = "2.1.1";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            pkg-config
            wayland-scanner
            makeWrapper
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            cairo
            pango
            json_c
            libxkbcommon
            glib
            librsvg
            gdk-pixbuf
          ];

          buildPhase = ''
            make
          '';

          installPhase = ''
            mkdir -p $out/bin
            mkdir -p $out/share/snappy-switcher/themes
            mkdir -p $out/share/doc/snappy-switcher

            install -m 755 snappy-switcher $out/bin/
            install -m 644 themes/*.ini $out/share/snappy-switcher/themes/
            install -m 644 config.ini.example $out/share/doc/snappy-switcher/
            install -m 644 README.md $out/share/doc/snappy-switcher/ || true

            # Install and wrap shell scripts with proper PATH for NixOS
            install -m 755 scripts/snappy-wrapper.sh $out/bin/snappy-wrapper
            wrapProgram $out/bin/snappy-wrapper \
              --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.coreutils pkgs.gnugrep pkgs.gnused ]}

            install -m 755 scripts/install-config.sh $out/bin/snappy-install-config
            wrapProgram $out/bin/snappy-install-config \
              --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.coreutils pkgs.gnugrep pkgs.gnused ]}
          '';

          meta = with pkgs.lib; {
            description = "A fast, keyboard-driven window switcher for Wayland compositors";
            homepage = "https://github.com/OpalAayan/snappy-switcher";
            license = licenses.gpl3;
            platforms = platforms.linux;
            maintainers = [ ];
            mainProgram = "snappy-switcher";
          };
        };

        # Development shell with all dependencies
        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [
            gcc
            gnumake
            gdb
          ];
        };
      }
    );
}
