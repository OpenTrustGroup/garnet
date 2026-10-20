#!/bin/bash

if [[ -d "src" ]]; then
  echo "src already existed"
  exit 1
fi

git clone ssh://git@gitlab.grt.sy:16022/application-product-group/hyp-license_test.git src
