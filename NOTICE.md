# Notices and credits

Camp Funds Withdraw is © 2026 Marco Luzzaro ("Marukoh"), all rights reserved: see [LICENSE](LICENSE).

## Credits
- `tools/cdextract.py` and `tools/cddecrypt.py` are original implementations written from public
  documentation of the Crimson Desert archive format:
  - the "Dev PAZ and PAMT Archives" page of the UltimateCameraMod wiki by fitzforge
    (<https://github.com/fitzforge/UltimateCameraMod>, MIT License), for the PAMT/PAZ layout and the
    ChaCha20 key derivation;
  - the community Crimson Desert modding tools by NattKh (CrimsonDesertModdingTools, MIT License),
    read as a reference for the same format.
  No code from these projects is included; they are credited for the format knowledge.
- Bob Jenkins' lookup3 hash (`hashlittle`, public domain) and the LZ4 block format (BSD-2-Clause
  specification by Yann Collet) are reimplemented from their public specifications.
- Crimson Desert and its data are property of Pearl Abyss. No game files are included.
