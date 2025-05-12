#!/bin/bash

# Initialize arrays
declare -A desktop_entries
declare -A file_locations

# Function to resolve icon name to file path
resolve_icon_path() {
    local icon_name="$1"
    local icon_path=""
    
    # Standard icon directories
    local icon_dirs=(
        "$HOME/.local/share/icons"
        "/usr/share/icons"
        "/usr/local/share/icons"
        "/var/lib/flatpak/exports/share/icons"
        "/usr/share/pixmaps"
    )
    
    # Get current icon theme (fallback to 'Papirus' if not set)
    local icon_theme=$(gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null | tr -d "'")
    [[ -z "$icon_theme" ]] && icon_theme="Papirus"
    
    # Common icon sizes to check
    local sizes=("1024x1024" "512x512" "256x256" "128x128" "96x96" "64x64" "48x48" "64x64" "32x32" "24x24" "16x16" "scalable")
    
    # If icon_name is already a full path
    if [[ -f "$icon_name" ]]; then
        echo "$icon_name"
        return
    fi

    # Remove only known image extensions, if present
    case "$icon_name" in
        *.png|*.svg|*.xpm)
            icon_name="${icon_name%.*}"
            ;;
    esac
    # Remove any extension if provided
    # icon_name="${icon_name%.*}"
    
           # Search for the icon
    for dir in "${icon_dirs[@]}"; do
        # Check theme-specific paths
        if [[ -d "$dir/$icon_theme" ]]; then
            # Prioritize scalable (SVG) first
            if [[ -f "$dir/$icon_theme/scalable/apps/$icon_name.svg" ]]; then
                echo "$dir/$icon_theme/scalable/apps/$icon_name.svg"
                return
            fi
            
            # Check raster sizes in descending order
            for size in "${sizes[@]}" ; do
                [[ "$size" == "scalable" ]] && continue # Skip scalable here since already checked
                for ext in "png" "svg"; do
                    if [[ -f "$dir/$icon_theme/$size/apps/$icon_name.$ext" ]]; then
                        echo "$dir/$icon_theme/$size/apps/$icon_name.$ext"
                        return
                    fi
                done
            done
        fi
        
        # Check Papirus theme as fallback
        if [[ "$icon_theme" != "Papirus" && -d "$dir/Papirus" ]]; then
            # Prioritize scalable (SVG) first
            if [[ -f "$dir/Papirus/scalable/apps/$icon_name.svg" ]]; then
                echo "$dir/Papirus/scalable/apps/$icon_name.svg"
                return
            fi
            
            # Check raster sizes in descending order
            for size in "${sizes[@]}" ; do
                [[ "$size" == "scalable" ]] && continue
                for ext in "png" "svg"; do
                    if [[ -f "$dir/Papirus/$size/apps/$icon_name.$ext" ]]; then
                        echo "$dir/Papirus/$size/apps/$icon_name.$ext"
                        return
                    fi
                done
            done
        fi 
        # Check hicolor theme as fallback
        if [[ "$icon_theme" != "hicolor" && -d "$dir/hicolor" ]]; then
            # Prioritize scalable (SVG) first
            if [[ -f "$dir/hicolor/scalable/apps/$icon_name.svg" ]]; then
                echo "$dir/hicolor/scalable/apps/$icon_name.svg"
                return
            fi
            
            # Check raster sizes in descending order
            for size in "${sizes[@]}" ; do
                [[ "$size" == "scalable" ]] && continue
                for ext in "png" "svg"; do
                    if [[ -f "$dir/hicolor/$size/apps/$icon_name.$ext" ]]; then
                        echo "$dir/hicolor/$size/apps/$icon_name.$ext"
                        return
                    fi
                done
            done
        fi 
        # Check pixmaps directory for legacy icons
        if [[ -f "$dir/$icon_name.png" ]]; then
            echo "$dir/$icon_name.png"
            return
        fi
        if [[ -f "$dir/$icon_name.svg" ]]; then
            echo "$dir/$icon_name.svg"
            return
        fi
    done
    
    # Fallback: return empty string if icon not found
    echo ""
}

# Function to process a .desktop file
process_desktop_file() {
    local file="$1"
    local base_name="${file##*/}"
    base_name="${base_name%.desktop}"
    
    if [[ "$base_name" != *.desktop ]]; then
        local name="" exec="" comment="" icon="" nodisplay=""
        local in_desktop_entry=0
        
        mapfile -t lines < "$file"
        for line in "${lines[@]}"; do
            [[ -z "$line" ]] && continue
            
            if [[ "$line" == "[Desktop Entry]" ]]; then
                in_desktop_entry=1
                continue
            elif [[ "$line" == "["*"]" && $in_desktop_entry -eq 1 ]]; then
                break
            fi
            
            if [[ $in_desktop_entry -eq 1 ]]; then
                IFS='=' read -r key value <<< "$line"
                case "$key" in
                    "Name") name="${value//\"/}" ;;
                    "Exec") 
                        exec="${value//\"/}"
                        if [[ "${exec: -3:2}" == " %" ]]; then
                            exec="${exec%???}"  # Remove last 3 characters
                        fi
                        ;;
                    "Comment") comment="${value//\"/}" ;;
                    "Icon") icon="${value//\"/}" ;;
                    "NoDisplay") nodisplay="${value//\"/}" ;;
                esac
            fi
        done
        
        if [[ -n "$name" && ( -z "$nodisplay" || "$nodisplay" != "true" ) ]]; then
            # Resolve icon to file path
            local icon_path=$(resolve_icon_path "$icon")
            # Store as delimited string
            desktop_entries["$base_name"]="$name|$exec|$comment|$icon_path"
        fi
    fi
}

# Collect files
system_dir="/usr/share/applications"
user_dir="$HOME/.local/share/applications"
flatpak_dir="/var/lib/flatpak/exports/share/applications"

shopt -s nullglob
for file in "$system_dir"/*.desktop "$flatpak_dir"/*.desktop "$user_dir"/*.desktop; do
    if [[ -f "$file" ]]; then
        base_name="${file##*/}"
        base_name="${base_name%.desktop}"
        file_locations["$base_name"]="$file"
    fi
done

# Process files
for base_name in "${!file_locations[@]}"; do
    process_desktop_file "${file_locations[$base_name]}"
done

# Generate grouped JSON array output
json_output="["
row=""
count=0
first_row=1

for key in "${!desktop_entries[@]}"; do
    if [ $count -eq 0 ]; then
        if [ $first_row -eq 1 ]; then
            first_row=0
        else
            json_output+=","
        fi
        row="["
    else
        row+=","
    fi
    
    # Parse delimited string and create JSON
    IFS='|' read -r name exec comment icon <<< "${desktop_entries[$key]}"
    row+="{\"Name\":\"$name\",\"Exec\":\"$exec\",\"Tooltip\":\"$comment\",\"Icon\":\"$icon\"}"
    ((count++))
    
    if [ $count -eq 5 ]; then
        row+="]"
        json_output+="$row"
        count=0
    fi
done

# Close the last row if it has fewer than 5 items
if [ $count -gt 0 ]; then
    row+="]"
    json_output+="$row"
fi

json_output+="]"

# Print JSON
if command -v jq >/dev/null 2>&1; then
    echo "$json_output" | jq
else
    echo "$json_output"
fi

exit 0
