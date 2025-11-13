#!/bin/bash
source_dir="assets/source"
dest_dir="assets"
theme_file="scripts/theme.conf"

declare -A theme
while IFS='=' read -r key value || [[ -n $key ]]; do
    [[ $key =~ ^[[:space:]]*# ]] && continue
    [[ -z $key ]] && continue
    key="${key#"${key%%[![:space:]]*}"}"  # trim
    key="${key%"${key##*[![:space:]]}"}"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    theme["$key"]="$value"
done < "$theme_file"

find "$source_dir" -type f -name "*.svg" | while IFS= read -r src_path; do
    rel_path="${src_path#"${source_dir}/"}"
    dest_svg="$dest_dir/$rel_path"
    # dest_png="${dest_svg%.svg}.png"
    mkdir -p "$(dirname "$dest_svg")"
    cp "$src_path" "$dest_svg"
    for key in "${!theme[@]}"; do
        sed -i "s|#$key#|${theme[$key]}|g" "$dest_svg"
    done
    # magick "$dest_svg" -background none -layers flatten "$dest_png"
    # rm "$dest_svg"
    # rm "$dest_png"
done
