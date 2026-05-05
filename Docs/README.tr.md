# Unreal MCP Toolkit Dokumantasyonu

Bu proje icin ana dokumantasyon dili Ingilizcedir. Bu dosya Turkce ikinci dil
girisidir ve en onemli dokumanlara hizli yonlendirme saglar.

English documentation index: [README.md](README.md).

## Kisa Ozet

Unreal MCP Toolkit, Unreal Editor icinde calisan yerel otomasyon sunucusu ve
AI/MCP istemcisi saglar. Amaci AI asistanlarinin Unreal projelerini guvenli ve
tekrarlanabilir sekilde incelemesi, asset export etmesi, Blueprint/UI/Material
gibi alanlarda kontrollu degisiklik yapmasi ve editor/runtime diagnostiklerini
okumasidir.

Guncel yuzey:

- 179 native TCP komutu
- 198 MCP araci
- 19 client-only MCP araci
- 38 komut kategorisi
- 42 capability kaydi
- Guncel dogrulama hedefi: Unreal Engine 5.7

## Nereden Baslamali?

| Ihtiyac | Dokuman |
|---|---|
| AI asistani ayarlamak ve ilk komutlari calistirmak | [Usage/AI_QUICKSTART.md](Usage/AI_QUICKSTART.md) |
| UE 5.7 Win64 ve Linux paketleri almak | [Usage/BUILD_PLUGIN.md](Usage/BUILD_PLUGIN.md) |
| Export sistemi, formatlar ve commandlet kullanimi | [Usage/EXPORT_SYSTEM.md](Usage/EXPORT_SYSTEM.md) |
| Tum araclar ve kritik notlar | [Reference/AI_REFERENCE.md](Reference/AI_REFERENCE.md) |
| Generated tool katalogu | [../Resources/Generated/MCPToolkit_ToolCatalog.md](../Resources/Generated/MCPToolkit_ToolCatalog.md) |
| Yeni ozelliklerin hangi katmanda duracagi | [Reference/CAPABILITY_MATRIX.md](Reference/CAPABILITY_MATRIX.md) |
| UI transfer sureci | [AI_UI_Transfer/README.md](AI_UI_Transfer/README.md) |
| UI TSpec semasi, sablonlari ve dogrulama | [UI_TSpec/README.md](UI_TSpec/README.md) |
| CommonUI mimari notlari | [CommonUI_Architecture.md](CommonUI_Architecture.md) |

## Hızlı Kurulum

1. `MCPToolkit` klasorunu Unreal projenizin `Plugins/` klasorune kopyalayin.
2. Projeyi rebuild edin ve Unreal Editor'u acik tutun.
3. MCP destekli AI asistaninizi su Python istemcisine yonlendirin:

   ```text
   python Plugins/MCPToolkit/MCPClient/ai_widget_mcp_client.py
   ```

4. Ilk test icin:

   ```powershell
   python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py ping
   python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py list_commands
   ```

## UI Degisikligi Icin Zorunlu Kural

Production Widget Blueprint mutation yapmadan once bir TSpec bulunmali ve
dogrulamadan gecmelidir:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

Plugin bir host Unreal projesine kuruluyken:

```powershell
powershell -ExecutionPolicy Bypass -File Plugins/MCPToolkit/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/Tasarim/UI_TSpecs
```

Bilesen davranisi belirsizse once
[AI_UI_Transfer/component_recipes](AI_UI_Transfer/component_recipes) altindaki
tarifleri okuyun. Tarif yoksa production asset yerine minimal probe WBP ile
dogrulama yapin.

## Adlandirma Notu

Plugin adi artik `Unreal MCP Toolkit`, modul adi `MCPToolkit`, C++ prefix'i
`MCT` seklindedir. Bazi MCP resource URI'leri, tool adlari ve legacy ortam
degiskenleri uyumluluk icin hala `commonai` adini tasiyabilir.
