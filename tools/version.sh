#!/bin/sh

set -eu

LC_ALL=C
export LC_ALL

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
release_tag_pattern='^[0-9]{4}\.(0[1-9]|1[0-2])\.[1-9][0-9]*$'

usage()
{
  echo "Usage: $0 [--from-tag YYYY.MM.N]" >&2
  exit 2
}

version_from_tag()
{
  tag=$1

  if ! printf '%s\n' "$tag" | grep -Eq "$release_tag_pattern"; then
    echo "Invalid losles release tag: $tag (expected YYYY.MM.N)" >&2
    return 1
  fi

  printf '%s\n' "$tag"
}

if [ "$#" -gt 0 ]; then
  [ "$#" -eq 2 ] || usage
  [ "$1" = "--from-tag" ] || usage
  version_from_tag "$2"
  exit
fi

if ! git -C "$repository_dir" rev-parse --verify HEAD >/dev/null 2>&1; then
  if [ -e "$repository_dir/.git" ]; then
    echo "Git metadata is present but could not be read;" \
      "check repository ownership and safe.directory." >&2
    git -C "$repository_dir" rev-parse --verify HEAD
    exit 1
  fi
  printf '%s\n' "0+unknown"
  exit
fi

dirty=
tracked_changes=$(
  git -C "$repository_dir" status \
    --porcelain=v1 \
    --untracked-files=no
)
if [ -n "$tracked_changes" ]; then
  dirty=.dirty
fi

exact_tag=
for candidate in $(git -C "$repository_dir" tag --points-at HEAD); do
  if printf '%s\n' "$candidate" | grep -Eq "$release_tag_pattern"; then
    if [ -n "$exact_tag" ]; then
      echo "Multiple losles release tags point at HEAD: $exact_tag, $candidate" >&2
      exit 1
    fi
    exact_tag=$candidate
  fi
done

if [ -n "$exact_tag" ]; then
  version=$(version_from_tag "$exact_tag")
  if [ -n "$dirty" ]; then
    version=$version+dirty
  fi
  printf '%s\n' "$version"
  exit
fi

description=$(
  git -C "$repository_dir" describe \
    --tags \
    --long \
    --abbrev=12 \
    --match '[0-9][0-9][0-9][0-9].[0-9][0-9].[0-9]*' \
    HEAD 2>/dev/null || :
)

if [ -n "$description" ]; then
  commit_hash=${description##*-g}
  tag_and_distance=${description%-g*}
  distance=${tag_and_distance##*-}
  nearest_tag=${tag_and_distance%-*}

  version=$(version_from_tag "$nearest_tag")
  printf '%s+%s.g%s%s\n' "$version" "$distance" "$commit_hash" "$dirty"
  exit
fi

commit_hash=$(git -C "$repository_dir" rev-parse --short=12 HEAD)
printf '0+untagged.g%s%s\n' "$commit_hash" "$dirty"
