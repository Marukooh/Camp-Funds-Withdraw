# Nexus Mods page setup (manual, one-off)

**Page:** https://www.nexusmods.com/crimsondesert/mods/3946 (mod 3946). **Main file ID:** 8283327 (set as `NEXUSMODS_FILE_ID` in the GitHub
environment `nexusmods`; `NEXUSMODS_API_KEY` set by Marco on 2026-11-01).

Nexus has no API to create a mod page: it is created once on the website, then every update goes
through the **Release** GitHub workflow.

## Page fields
- **Name:** `Withdraw Camp Funds - Camp Funds to Silver`
- **Summary:** `nexusmods/short-description.txt`
- **Description:** `nexusmods/full-description.bbcode.txt` (BBCode editor)
- **Category:** Gameplay (or Miscellaneous if Gameplay isn't available)
- **Tags:** Gameplay, Economy/Money, User Interface, ASI, Quality of Life
- **Version:** `2.04.07`
- **Main image (thumbnail):** `art/thumbnail.png` (1280×720); also add `art/banner.png` and an
  `art/menu.png` (Carl's menu) and `art/modal.png` (slider) as images
- **Requirements:** Definitive Mod Manager (or any ASI loader); notes: "Linux/Steam Deck: winmm=n,b"
- **Permissions:** no re-upload, no modification, no asset use, no conversion, no use in other mods
  or mod packs; credit: "See NOTICE.md on GitHub"; donation points as preferred
- **Source code link:** https://github.com/Marukooh/Camp-Funds-Withdraw

## First file
Upload `Withdraw Camp Funds v2.04.07.zip` (from the GitHub release, or `tools/package.sh` then zip
`dist/*`) as a **Main** file named `Withdraw Camp Funds`, with "Mod Manager Download" enabled.

## Link the workflow
1. On the file you just uploaded, read its **file ID** (Nexus "Manage files": it's in the file's
   edit link or the "file_id" of the upload action docs).
2. GitHub → repo Settings → Environments → create `nexusmods`:
   - secret `NEXUSMODS_API_KEY` = your personal Nexus API key (nexusmods.com → Site preferences →
     API Keys);
   - variable `NEXUSMODS_FILE_ID` = the file ID above.
3. From then on: Actions → **Release** → Run workflow (version, changelog, "publish_nexus").
