ARG IDF_IMAGE
FROM ${IDF_IMAGE}
# for firmware/simdeck, which links libjpeg statically
RUN apt-get update && apt-get install -y --no-install-recommends libjpeg-turbo8-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*
