# STD Platform CI Package Chain

This document defines how each repository in the stack must publish and consume
Debian packages so that STD-platform CI builds are self-contained on bare
`ubuntu-latest` without the `ratos-dev-image` container.

The EVL build/test job continues to use the `ratos-dev-image` container and
RaTOS QEMU, and is unaffected by these changes.

---

## Dependency chain

```
reflectcpp  (header-only, external: getml/reflect-cpp)
    └── SeRTial
            └── CoreRaT  (STD platform — corerat_tims backend)
                    └── CommRaT
                            └── RaTGUI
```

Each repo must:
1. **Publish** a `<package>_<version>_amd64.deb` as a GitHub release asset.
2. **Install** all upstream `.deb` files before configuring CMake.

---

## Package naming convention

| Repository | GitHub repo | Release asset name |
|---|---|---|
| reflectcpp | `getml/reflect-cpp` | `reflectcpp_<version>_amd64.deb` |
| SeRTial | `mattih11/SeRTial` | `sertial_<version>_amd64.deb` |
| CoreRaT | `mattih11/CoreRaT` | `corerat-std_<version>_amd64.deb` |
| CommRaT | `mattih11/CommRaT` | `commrat_<version>_amd64.deb` |
| RaTGUI | `mattih11/RaTGUI` | `ratgui_<version>_amd64.deb` |

The `corerat-std` suffix distinguishes it from a future `corerat-evl` package.

---

## What each `.deb` must install

All packages install under the default CMake prefix `/usr/local`.

### reflectcpp

```
/usr/local/include/rfl/           (headers)
/usr/local/lib/cmake/reflectcpp/  (CMake config)
```

reflectcpp is header-only; the `.deb` contains only headers and the CMake
config. Build with:

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DREFLECTCPP_BUILD_SHARED=OFF -DREFLECTCPP_USE_BUNDLED_DEPENDENCIES=ON
cmake --build build
cmake --install build
```

### SeRTial

```
/usr/local/include/sertial/       (headers)
/usr/local/lib/cmake/SeRTial/     (CMake config including SeRTialSchemaGen.cmake)
/usr/local/share/sertial/         (viewer HTML, schema gen driver source)
```

### CoreRaT (STD platform)

Must be built with `-DCORERAT_PLATFORM=STD` so it exports `CoreRaT::corerat_tims`.

```
/usr/local/include/corerat/
/usr/local/lib/libcorerat_tims.a  (or .so)
/usr/local/lib/cmake/CoreRaT/
/usr/local/bin/corerat-router-tcp
```

The package name `corerat-std` (or a CMake component `STD`) makes it clear
which backend is installed. The EVL package is installed only inside the
`ratos-dev-image`.

### CommRaT

```
/usr/local/include/commrat/
/usr/local/lib/cmake/CommRaT/     (includes CommRaTSchemaGen.cmake, CommRaTMacros.cmake)
/usr/local/share/commrat/         (commrat-inspect viewer + schema gen driver)
/usr/local/bin/commrat            (CLI binary)
```

### RaTGUI

```
/usr/local/bin/ratgui
/usr/local/lib/cmake/RaTGUI/      (if downstream embeds it)
```

---

## CI workflow pattern

Use this pattern in each repo's `ci.yml`. Replace `<THIS_REPO>` with the
current repo name and fill in only the upstream steps needed.

```yaml
jobs:
  build-std:
    name: Build & Test (STD platform)
    runs-on: ubuntu-latest
    # No container — bare ubuntu-latest; deps installed from release packages below.
    steps:
      - uses: actions/checkout@v4

      # -----------------------------------------------------------------------
      # Install build tools
      # -----------------------------------------------------------------------
      - name: Install build tools
        run: |
          sudo apt-get update -qq
          sudo apt-get install -y --no-install-recommends \
            cmake ninja-build pkg-config

      # -----------------------------------------------------------------------
      # Install reflectcpp
      # All repos need this step.
      # -----------------------------------------------------------------------
      - name: Install reflectcpp
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release download --repo getml/reflect-cpp \
            --pattern "reflectcpp_*_amd64.deb" --dir /tmp/pkgs
          sudo dpkg -i /tmp/pkgs/reflectcpp_*_amd64.deb

      # -----------------------------------------------------------------------
      # Install SeRTial
      # Required by: CoreRaT, CommRaT, RaTGUI
      # -----------------------------------------------------------------------
      - name: Install SeRTial
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release download --repo mattih11/SeRTial \
            --pattern "sertial_*_amd64.deb" --dir /tmp/pkgs
          sudo dpkg -i /tmp/pkgs/sertial_*_amd64.deb

      # -----------------------------------------------------------------------
      # Install CoreRaT (STD platform)
      # Required by: CommRaT, RaTGUI
      # -----------------------------------------------------------------------
      - name: Install CoreRaT (STD)
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release download --repo mattih11/CoreRaT \
            --pattern "corerat-std_*_amd64.deb" --dir /tmp/pkgs
          sudo dpkg -i /tmp/pkgs/corerat-std_*_amd64.deb

      # -----------------------------------------------------------------------
      # Install CommRaT
      # Required by: RaTGUI
      # -----------------------------------------------------------------------
      - name: Install CommRaT
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release download --repo mattih11/CommRaT \
            --pattern "commrat_*_amd64.deb" --dir /tmp/pkgs
          sudo dpkg -i /tmp/pkgs/commrat_*_amd64.deb

      # -----------------------------------------------------------------------
      # Build and test <THIS_REPO>
      # Always set COMMRAT_PLATFORM=STD (or CORERAT_PLATFORM=STD) explicitly
      # so the build does not accidentally pick up an EVL installation.
      # -----------------------------------------------------------------------
      - name: Configure
        run: cmake --preset default   # preset must set COMMRAT_PLATFORM=STD

      - name: Build
        run: cmake --build --preset default --parallel $(nproc)

      - name: Test
        run: |
          corerat-router-tcp > /dev/null 2>&1 &
          sleep 1
          ctest --preset default --output-on-failure
          kill %1 2>/dev/null || true

  # -----------------------------------------------------------------------
  # Publish .deb on release tags
  # -----------------------------------------------------------------------
  publish-deb:
    name: Publish STD package
    runs-on: ubuntu-latest
    if: startsWith(github.ref, 'refs/tags/v')
    needs: build-std
    steps:
      - uses: actions/checkout@v4

      # ... (install upstream deps same as above) ...

      - name: Build and package
        run: |
          cmake --preset default -DCMAKE_INSTALL_PREFIX=/usr/local
          cmake --build --preset default --parallel $(nproc)
          # CPack or manual dpkg-deb to produce <package>_<version>_amd64.deb
          cpack -G DEB --preset default

      - name: Upload to release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release upload "${{ github.ref_name }}" \
            build/default/*.deb --clobber
```

---

## CMake preset requirement

Each repo's `default` preset (and any STD-platform preset) must explicitly
force the STD backend. Add to `CMakePresets.json`:

```json
{
  "name": "default",
  "inherits": "base",
  "cacheVariables": {
    "COMMRAT_PLATFORM": "STD",
    "COMMRAT_BUILD_EXAMPLES": "ON",
    "COMMRAT_BUILD_TESTS": "ON"
  }
}
```

For CoreRaT itself, use `CORERAT_PLATFORM=STD` instead.

Without this, a system that also has `libevl` headers installed (as the
`ratos-dev-image` does) will silently build the EVL backend and fail at
runtime on a non-EVL kernel.

---

## Per-repo step matrix

Include only the steps your repo actually needs:

| Repo | reflectcpp | SeRTial | CoreRaT-STD | CommRaT |
|---|---|---|---|---|
| reflectcpp | — | — | — | — |
| SeRTial | install | — | — | — |
| CoreRaT | install | install | — | — |
| CommRaT | install | install | install | — |
| RaTGUI | install | install | install | install |

---

## Current installed versions (as of ratos-dev-image:latest / v0.1.1)

| Package | Version |
|---|---|
| reflectcpp | 0.23.0 |
| SeRTial | 2.0.0 |
| CoreRaT | 0.1.0 |
| CommRaT | (see CMake config in repo) |
