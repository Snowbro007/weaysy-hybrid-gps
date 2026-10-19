# Weaysy Hybrid GPS — Nederlands

Deze experimentele download laat de HUD-GPS en ondersteunde cabine-GPS samen
wisselen tussen dag en nacht. Alleen **ATS 1.61.3.18, Windows x64 en DirectX 11**
zijn momenteel ondersteund. ETS2 en ProMods zijn voor deze download niet getest.

1. Download de ZIP via **Releases** en pak alles uit.
2. Sluit ATS helemaal af. Installeer apart **ReShade 6.8.0 met full add-on support**
   via https://reshade.me/ voor `bin/win_x64/amtrucks.exe`. Kies DirectX 10/11/12.
   Er is geen effectpreset nodig. Start ATS zelf in DX11.
3. Start `WeaysySetup.exe`. Die controleert de exacte spelversie en loader,
   maakt herstelkopieën en installeert de twee native modules plus instellingen.
4. Kopieer `weaysy_ats_hybrid_gps.scs` naar de `mod`-map van je actieve ATS-profielmap.
   Normaal: `Documenten/American Truck Simulator/mod`. Bij `-homedir` is dit anders.
5. Activeer alleen **Weaysy Hybrid GPS — Day and Night (ATS Preview)** als GPS-thema.
   Zet statische Light/Dark-versies, oude proefversies en andere GPS-kleurmods uit.
6. Start ATS. De standaard SDK-bevestiging kan verschijnen; accepteer die als je
   de plugin wilt gebruiken. Geen SDK-download, developer mode of Python nodig.

Standaard is het dag vanaf 06:00 en nacht vanaf 20:00, volgens de lokale speltijd.
Dit zijn instelbare tijden, geen berekende zonsopkomst/zonsondergang.
Instellingen staan in `bin/win_x64/plugins/WeaysyTimeReader.ini`.
Laat `UseLocalTimeZone=1` en `SwitchEveryGameMinutes=0` staan voor normaal spelen.

Terug naar een Workshop-thema: sluit ATS, start `DISABLE.cmd` en deactiveer de
hybride SCS. Alleen de bekende Weaysy-companionbestanden worden uitgeschakeld.
ReShade zelf, andere mods, profielen en saves blijven staan.

Crash-/pech-/wegwerkmeldingen zitten niet in deze download. De volledige Engelse
uitleg, configuratie en herstelstappen staan in [INSTALL.md](INSTALL.md).
