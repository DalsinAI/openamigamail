# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created OpenMail, designs it and maintains it.

## The AmigaChrome team

We are The AmigaChrome Team, who build OpenMail alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, who started the work on SacredTrees's x86 cores.
- **The threads**, each one taking a piece of the work from design to release.

## Copyright holder

OpenMail's own code, providers, icons and documents are
Copyright (c) 2026 AmigaChrome Team, released under the MIT licence (`LICENSE`).

## Third-party work in this repository

| Component | Where | Authors | Licence |
| --- | --- | --- | --- |
| Boxie icons: the drawer icon, converted to OS 3.2 GlowIcons | `package/icons/Drawer.info` | Damir Šijaković | MIT (`package/icons/LICENCE.Boxie`) |

### From our other repositories

- **OpenGadTools** (`DalsinAI/opengadtools`): a copy of its library in `third_party/opengadtools`, and the themes in `package/Themes/`, MIT, AmigaChrome Team.

## Work we learned from

OpenMail is written from the standards and the providers' OAuth notes.

- **SimpleMail**: a checklist of what a mail client on the Amiga does. None of its code is used.

## Used at build or run time, not included

- **OpenTLS** (`opentls.library`, the Team's own, DalsinAI/openamigatls): TLS on the Amiga, opened from the copy installed there. (OpenMail 0.1 to 0.3.2 also used AmiSSL 5 that way.)
- **OpenSSL 3**: TLS for the host tests.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
