#!/bin/bash

declare -A desktop_entries
declare -A file_locations

resolve_iPath() {
    local iName="$1"
    local iPath=""

    local icon_dirs=(
        "$HOME/.local/share/icons"
        "/usr/share/icons"
        "/usr/local/share/icons"
        "/var/lib/flatpak/exports/share/icons"
        "/usr/share/pixmaps"
    )

    local icon_theme
    icon_theme=$(gsettings get org.gnome.desktop.interface icon-theme \
        2>/dev/null | tr -d "'")
    [[ -z "$icon_theme" ]] && icon_theme="Papirus"

    local sizes=("1024x1024" "512x512" "256x256" "128x128" "96x96" "64x64" \
        "48x48" "64x64" "32x32" "24x24" "16x16" "scalable")

    if [[ -f "$iName" ]]; then
        echo "$iName"
        return
    fi

    case "$iName" in
        *.png|*.svg|*.xpm)
            iName="${iName%.*}"
            ;;
    esac

    for dir in "${icon_dirs[@]}"; do
        if [[ -d "$dir/$icon_theme" ]]; then
            if [[ -f "$dir/$icon_theme/scalable/apps/$iName.svg" ]]; then
                echo "$dir/$icon_theme/scalable/apps/$iName.svg"
                return
            fi

            for size in "${sizes[@]}" ; do
                [[ "$size" == "scalable" ]] && continue
                for ext in "png" "svg"; do
                    if [[ -f "$dir/$icon_theme/$size/apps/$iName.$ext" ]]; then
                        echo "$dir/$icon_theme/$size/apps/$iName.$ext"
                        return
                    fi
                done
            done
        fi

        if [[ "$icon_theme" != "Papirus" && -d "$dir/Papirus" ]]; then
            if [[ -f "$dir/Papirus/scalable/apps/$iName.svg" ]]; then
                echo "$dir/Papirus/scalable/apps/$iName.svg"
                return
            fi

            for size in "${sizes[@]}" ; do
                [[ "$size" == "scalable" ]] && continue
                for ext in "png" "svg"; do
                    if [[ -f "$dir/Papirus/$size/apps/$iName.$ext" ]]; then
                        echo "$dir/Papirus/$size/apps/$iName.$ext"
                        return
                    fi
                done
            done
        fi
        if [[ "$icon_theme" != "hicolor" && -d "$dir/hicolor" ]]; then
            if [[ -f "$dir/hicolor/scalable/apps/$iName.svg" ]]; then
                echo "$dir/hicolor/scalable/apps/$iName.svg"
                return
            fi

            for size in "${sizes[@]}" ; do
                [[ "$size" == "scalable" ]] && continue
                for ext in "png" "svg"; do
                    if [[ -f "$dir/hicolor/$size/apps/$iName.$ext" ]]; then
                        echo "$dir/hicolor/$size/apps/$iName.$ext"
                        return
                    fi
                done
            done
        fi
        if [[ -f "$dir/$iName.png" ]]; then
            echo "$dir/$iName.png"
            return
        fi
        if [[ -f "$dir/$iName.svg" ]]; then
            echo "$dir/$iName.svg"
            return
        fi
    done

    echo ""
}

process_desktop_file() {
    local file="$1"
    local base_name="${file##*/}"
    base_name="${base_name%.desktop}"

    if [[ "$base_name" != *.desktop ]]; then
        local name="" exec="" comment="" icon="" hide=""
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
                            exec="${exec%???}"
                        fi
                        ;;
                    "Comment") comment="${value//\"/}" ;;
                    "Icon") icon="${value//\"/}" ;;
                    "NoDisplay") hide="${value//\"/}" ;;
                esac
            fi
        done

        if [[ -n "$name" && ( -z "$hide" || "$hide" != "true" ) ]]; then
            local iPath
            iPath=$(resolve_iPath "$icon")
            desktop_entries["$base_name"]="$name|$exec|$comment|$iPath"
        fi
    fi
}

# Collect files
system_dir="/usr/share/applications"
user_dir="$HOME/.local/share/applications"
flatpak_dir="/var/lib/flatpak/exports/share/applications"

shopt -s nullglob
for file in "$system_dir"/*.desktop \
    "$flatpak_dir"/*.desktop \
    "$user_dir"/*.desktop; do
    if [[ -f "$file" ]]; then
        base_name="${file##*/}"
        base_name="${base_name%.desktop}"
        file_locations["$base_name"]="$file"
    fi
done

for base_name in "${!file_locations[@]}"; do
    process_desktop_file "${file_locations[$base_name]}"
done

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

    IFS='|' read -r name exec comment icon <<< "${desktop_entries[$key]}"
    row+="{\"Name\":\"$name\",\"Exec\":\"$exec\",\"Tooltip\":\"$comment\",\
        \"Icon\":\"$icon\"}"
    $((count++))

    if [ $count -eq 5 ]; then
        row+="]"
        json_output+="$row"
        count=0
    fi
done

if [ $count -gt 0 ]; then
    row+="]"
    json_output+="$row"
fi

json_output+="]"

if command -v jq >/dev/null 2>&1; then
    echo "$json_output" | jq
else
    echo "$json_output"
fi

exit 0
