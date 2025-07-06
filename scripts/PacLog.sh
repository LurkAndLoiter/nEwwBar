#!/bin/env bash
if [[ $1 == "today" ]]; then
  SearchString="$(date +%Y-%m-%d)"
elif [[ $1 == "month" ]]; then
  SearchString="$(date +%Y-%m)"
elif [[ $1 == "year" ]]; then 
  SearchString="$(date +%Y)"
elif [[ $1 == "all" ]]; then 
  SearchString=""
else # default value is last week
  SearchString="$(date +%Y-%m-%d)|$(date -d 'last sunday' +%Y-%m-%d)|$(date -d 'last monday' +%Y-%m-%d)|$(date -d 'last tuesday' +%Y-%m-%d)|$(date -d 'last wednesday' +%Y-%m-%d)|$(date -d 'last thursday' +%Y-%m-%d)|$(date -d 'last friday' +%Y-%m-%d)|$(date -d 'last saturday' +%Y-%m-%d)"
fi

# Get list of installed packages (name only) into a temporary file
pacman -Q | awk '{print $1}' > /tmp/installed_packages.txt

# Process pacman.log and generate JSON with installed status
awk -v query="$SearchString" -v installed_file="/tmp/installed_packages.txt" '
BEGIN {
  # Read installed packages into an array
  while ((getline line < installed_file) > 0) {
    installed[line] = 1
  }
  close(installed_file)
}
$0 ~ query && $3 ~ /^(Running|transaction|upgraded|installed|removed)$/ {
  if ($3 == "Running") {
    pacman_line=$0
    pacman_pending=1
  } else if (pacman_pending && $3 == "transaction" && $0 ~ /transaction started/) {
    print pacman_line
    pacman_pending=0
    print $0
  } else if ($3 == "transaction" || $3 == "upgraded" || $3 == "installed" || $3 == "removed") {
    pacman_pending=0
    print $0
  }
}' /var/log/pacman.log | awk -v installed_file="/tmp/installed_packages.txt" '
BEGIN {
  # Re-read installed packages for the second awk
  while ((getline line < installed_file) > 0) {
    installed[line] = 1
  }
  close(installed_file)
}
{
  if ($2 == "[PACMAN]") {
    printf "{\"transaction\":\"" $1 "\","
    $1=$2=$3=""
    printf "\"command\":\"" $0 "\",\"packages\": "
  } else if ($3 == "transaction" && $4 == "started") {
    printf "["
  } else if ($3 == "transaction" && $4 == "completed") {
    printf "\n"
  } else {
    pkg_name = $4
    is_installed = (pkg_name in installed) ? "true" : "false"
    printf "{\"name\": \"" $4 "\", \"change\": \"" $3 "\", "
    $1=$2=$3=$4=""
    printf "\"version\": \"" $0 "\", \"installed\": " is_installed "},"
  }
}' | sed 's/,*$/]},/' \
  | sed 's/    //g' \
  | sed "s/'.*\/yay\/.*'/'AUR call by yay'/g" \
  | sed 's/   //g' \
  | awk '{printf $0}' \
  | sed 's/,*$/]/' \
  | sed 's/^/[/' \
  | jq '. | reverse'

# Clean up temporary file
rm /tmp/installed_packages.txt
