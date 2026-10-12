# Attribution

The standalone mods were made by DieuDeGlace with the aid of OpenAI ChatGPT/Codex. The SDK headers and sources under `vendor/cwmods` come from Gijs/gijsgroenewegen's MIT-licensed CubeMegaMod bundle, with local loader API 7.3 compatibility edits. Their original license is retained at `vendor/cwmods/LICENSE`. Upstream: https://github.com/gijsgroenewegen/CubeMegaMod and https://github.com/gijsgroenewegen/CWSDK.

The game, its assets, Microsoft/LLVM development tools, and the external CubeModLoader are not included and remain the property of their respective authors. Mod source uses native addresses verified against a specific Steam executable; this is not an official Cube World project.

SMAA shader and lookup tables under `vendor/SMAA` are from [iryoku/smaa](https://github.com/iryoku/smaa), commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`. Copyright Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez. The upstream MIT-style license and source notices are retained. SMAA.hlsl comments were normalized from Windows-1252 to UTF-8; build-time sampler declarations are assigned explicit registers for native D3D11 use. Integration code was made with AI assistance.
