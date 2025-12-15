# jcode - Modern Terminal Editor

Cuman di test di arch linux

## Fitur

### Editing
- Auto-indent saat enter
- Bracket matching (Ctrl+B)
- Format document (Ctrl+L)
- Duplicate line (Ctrl+D)
- Toggle comment (Ctrl+/)
- Undo/Redo (Ctrl+Z/Y)
- Copy/Cut/Paste (Ctrl+C/X/V)
- **Mouse selection** - Click & drag untuk select text

### UI
- Command palette (Ctrl+P)
- File explorer sidebar
- Minimap (fixed)
- **Image Viewer** - View images in terminal (click image file in explorer)
- Search panel (Ctrl+F)
- Multiple tabs
- Syntax highlighting

### Konfigurasi
- Config file di `~/.config/jcode/config`
- Bisa set: tab size, auto indent, explorer width, dll

## Keybindings

**File:** Ctrl+N (new), Ctrl+O (explorer), Ctrl+S (save), Ctrl+W (close), Ctrl+Q (quit)

**Edit:** Ctrl+Z (undo), Ctrl+Y (redo), Ctrl+C (copy), Ctrl+X (cut), Ctrl+V (paste), Ctrl+A (select all)

**Features:** Ctrl+P (palette), Ctrl+F (search), Ctrl+M (minimap), Ctrl+B (bracket), Ctrl+L (format), Ctrl+D (duplicate), Ctrl+/ (comment)

**Navigation:** Arrow keys, Home/End, Page Up/Down

**Mouse:** Click untuk move cursor, Drag untuk select text

**Image Viewer:** 
- Click image file di explorer untuk open
- Press 'q' untuk close
- Press 'o' untuk open dengan external viewer

## Build & Run

```bash
make
./bin/jcode [file]
```

Config otomatis dibuat di `~/.config/jcode/config` saat pertama kali run.
