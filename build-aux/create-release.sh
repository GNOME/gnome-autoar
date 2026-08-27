#!/bin/bash
set -e

# Usage: create_release.sh [optional next version]

meson_file="meson.build"
news_file="NEWS"

### Version ###

meson_version=$(grep " version: '" ${meson_file} | cut -d\' -f2 | head -n 1)
release_version=${1:-"${meson_version}"}

current_branch=$(git rev-parse --abbrev-ref HEAD)
latest_commit=$(git rev-parse HEAD)
previous_tag=$(git describe --tags --abbrev=0)

# Calculate next version
IFS='.' read -r major minor patch <<< "${release_version}"
if [[ ! ( "$major" =~ ^[0-9]+$ && "$minor" =~ ^[0-9]+$ && "$patch" =~ ^[0-9]+$ ) ]]; then
	echo "Error: Don't know how to handle version '${release_version}'"
	exit 1
else
	# otherwise increase patch
	next_version="${major}.${minor}.$((patch+1))"
fi

### Commits ###

bump_commit_msg="Post[\ -]release version bump"
translation_filter="^Update.*translation\$"
commits=$(git log \
	--pretty=format:"%h %s (%an)" \
	${previous_tag}..${latest_commit} \
	--invert-grep --grep="${bump_commit_msg}" --grep="${translation_filter}")
t10n_commits=$(git log --pretty=oneline \
	${previous_tag}..${latest_commit} \
	--grep="${translation_filter}")

num_commits=$(echo "${commits}" | sed '/^\s*$/d' | wc -l)
num_t10n_commits=$(echo "${t10n_commits}" | sed '/^\s*$/d' | wc -l)

meson_version_str=""
if [ ! ${release_version} = ${meson_version} ] ; then
	meson_version_str=" (meson has ${meson_version})"
fi
echo "Creating release ${release_version}${meson_version_str} (Bumping to ${next_version})"
echo "on top of commit ${latest_commit}"
echo ""
echo "There were ${num_commits} code change(s) and ${num_t10n_commits} translation change(s) since ${previous_tag}."
echo ""

### NEWS ###

header="Major changes in ${release_version}:"
news_update="\
${header}
* 

${commits}
"

echo "${news_update}" | cat - NEWS > temp && mv temp NEWS

echo "Edit the NEWS file then continue with [Enter]"
read

release_notes=$(git diff --unified=0 --color=never NEWS | grep -E '^\+[^+]' | sed 's/^\+//')

### Release commit ###
release_message="Release ${release_version}"
git add "${news_file}"
git commit -m "${release_message}"
release_commit=$(git rev-parse HEAD)

### Post-Release bump commit ###
sed -ri "s/  version: [^,]*,/  version: '${next_version}',/" "${meson_file}"

git add "${meson_file}"
git commit -m "Post-release version bump"

### Note about release tagging ###
release_notes="${release_message}
${release_notes}"
echo Release commits created. Tag release with:
echo "git tag -s ${release_version} HEAD^ -m \"${release_notes}\""
echo "git push --tags"
