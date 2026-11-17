#pragma once

// Keep the value a plain quoted literal, and the only digits on the line, otherwise it won't be parsed by app.pro correctly
#define GG_VERSION "1.5.0"

// "owner/name" on GitHub: home of the releases and the issue tracker
#define GG_GITHUB_REPOSITORY "VioletGiraffe/GoodGit"

// The release assets' name without the architecture and the extension, as installer.iss and scripts/create_*.sh name them
#define GG_RELEASE_ASSET_NAME "GoodGit"
