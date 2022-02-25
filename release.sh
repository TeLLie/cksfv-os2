#!/bin/bash

version=${1}
gpgkey=${2}

if [[ -z ${gpgkey} ]] ; then
    echo "give version, e.g. 1.3.15, and gpg user-id, e.g. 'My Name'"
    exit 1
fi

blobname="cksfv-${version}.tar.bz2"
echo "Creating blob ${blobname} and signature ${blobname.asc}"

git tag -s -u "${gpgkey}" "cksfv-${version}"

git archive --prefix="cksfv-${version}/" HEAD |bzip2 >"${blobname}"

gpg --detach-sign -u "${gpgkey}" --armor "${blobname}"

echo "Remember to git push --tag"
