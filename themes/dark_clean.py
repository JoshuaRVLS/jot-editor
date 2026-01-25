import jcode_api
import sys

try:
    print("Loading dark_clean theme...")
    
    # Base colors
    BG_COLOR = 235  # Dark grey
    FG_DEFAULT = 7  # White/Grey
    
    # Backgrounds
    jcode_api.set_theme_color("default", FG_DEFAULT, BG_COLOR)
    jcode_api.set_theme_color("panel_border", 240, BG_COLOR) 
    jcode_api.set_theme_color("line_num", 240, BG_COLOR)
    jcode_api.set_theme_color("status", 250, 238) # Slightly lighter grey for status
    jcode_api.set_theme_color("command", 250, 238)
    jcode_api.set_theme_color("selection", 255, 24) # Dark blue for selection
    
    # Syntax (Using BG_COLOR instead of -1 to match background)
    jcode_api.set_theme_color("keyword", 208, BG_COLOR) # Orange
    jcode_api.set_theme_color("string", 142, BG_COLOR) # Greenish
    jcode_api.set_theme_color("comment", 243, BG_COLOR) # Grey
    jcode_api.set_theme_color("number", 141, BG_COLOR) # Purple
    jcode_api.set_theme_color("function", 39, BG_COLOR) # Blue
    jcode_api.set_theme_color("type", 214, BG_COLOR) # Yellow-Orange
    jcode_api.set_theme_color("bracket_match", 15, 240) # Bright white on grey

    # UI
    jcode_api.set_theme_color("cursor", 0, 15) # Black on White block
    jcode_api.set_theme_color("minimap", 243, BG_COLOR) # Grey on Dark
    
    # Additional Elements
    jcode_api.set_theme_color("telescope", FG_DEFAULT, BG_COLOR)
    jcode_api.set_theme_color("telescope_selected", 255, 238)
    
    print("Theme dark_clean loaded successfully!")

except Exception as e:
    print(f"Error loading dark_clean theme: {e}")
