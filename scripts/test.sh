#!/bin/bash

# Initialize arrays
declare -A desktop_entries
declare -A file_locations

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
            # Store as delimited string instead of JSON
            desktop_entries["$base_name"]="$name|$exec|$comment|$icon"
        fi
    fi
}

# Collect files
system_dir="/usr/share/applications"
user_dir="$HOME/.local/share/applications"

shopt -s nullglob
for file in "$system_dir"/*.desktop "$user_dir"/*.desktop; do
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
