from jcode_api import vim

theme = {
    "Normal": {"fg": 252, "bg": 234},
    "LineNr": {"fg": 239, "bg": 234},
    "Comment": {"fg": 244, "bg": 234},
    "Keyword": {"fg": 81, "bg": 234},
    "String": {"fg": 114, "bg": 234},
    "Number": {"fg": 179, "bg": 234},
    "Function": {"fg": 223, "bg": 234},
    "Type": {"fg": 110, "bg": 234},
    "Cursor": {"fg": 234, "bg": 252},
    "Visual": {"fg": 234, "bg": 110},
    "StatusLine": {"fg": 252, "bg": 236},
    "FloatBorder": {"fg": 239, "bg": 234},
    "Pmenu": {"fg": 252, "bg": 236},
    "PmenuSel": {"fg": 234, "bg": 110},
    "TelescopeNormal": {"fg": 252, "bg": 235},
    "TelescopeSelection": {"fg": 234, "bg": 110},
    "TelescopePreviewNormal": {"fg": 252, "bg": 234},
}

for group, spec in theme.items():
    vim.api.nvim_set_hl(0, group, spec)
