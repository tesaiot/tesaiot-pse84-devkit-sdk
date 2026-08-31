# BENTO Secure Library — SDK reference

Generated from the shipped headers and cross-checked against the shipped
binary: a function appears here only if `nm` found it in the archive.
Regenerate with `./bento-release.sh docs`.

| Header | Archive API | Open-source fns | What it is |
|---|---|---|---|
| [optiga_lib_config_mtb.h](optiga_lib_config_mtb.md) | 0 | 0 | This file contains part of the Platform Abstraction Layer. This is a platform specific file. Thi... |
| [optiga_oid_config.h](optiga_oid_config.md) | 0 | 0 | configuration / constants |
| [optiga_psa_se.h](optiga_psa_se.md) | 0 | 5 | Header file for OPTIGA PSA Secure Element driver Related Document: See README.md |
| [optiga_trust_helpers.h](optiga_trust_helpers.md) | 0 | 17 | This file contains helping fucntions to read a certificate or change some default parameter Rela... |
| [tesaiot_hsm_api.h](tesaiot_hsm_api.md) | 18 | 0 | The complete API of libbento_hsm.a — every function the archive exports, and nothing it does not... |

**18 archive API functions across 5 headers**, plus 22 functions declared here whose implementation ships as open source in this package (yours to read and change). "Archive API" means `nm` found the symbol exported by the shipped binary; `api.txt` lists 18 exported symbols in total — the difference is data symbols and functions whose only declaration is in `bento_secure_undeclared.h`.
