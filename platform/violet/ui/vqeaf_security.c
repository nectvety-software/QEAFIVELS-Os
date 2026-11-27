/* SPDX-License-Identifier: MIT */
/* Curated offline CVE metadata; no exploit payloads or execution paths. */
#include "vqeaf_security.h"

#include <ctype.h>
#include <string.h>

static const vqeaf_advisory_t catalog[] = {
    {
        "CVE-2021-44228",
        "Log4Shell - Apache Log4j2 remote code execution",
        "Apache Log4j2",
        "2021-12-10",
        "Critical",
        "NVD",
        "Log4j2 versions 2.0-beta9 through 2.15.0 can perform unsafe JNDI lookups that may lead to remote code execution.",
        "Upgrade to a fixed Log4j2 release and disable unnecessary JNDI lookups."
    },
    {
        "CVE-2017-0144",
        "EternalBlue - Windows SMBv1 remote code execution",
        "Microsoft Windows SMBv1",
        "2017-03-14",
        "Critical",
        "Microsoft",
        "Crafted SMBv1 messages can trigger remote code execution on affected Windows systems.",
        "Install Microsoft's security update and disable SMBv1 where it is not required."
    },
    {
        "CVE-2020-1472",
        "Zerologon - Windows Netlogon privilege escalation",
        "Microsoft Windows Netlogon",
        "2020-08-11",
        "Critical",
        "Microsoft",
        "Improper validation of Netlogon secure-channel credentials may allow privilege escalation in an affected domain environment.",
        "Apply Microsoft's update and enforce the recommended secure Netlogon settings."
    },
    {
        "CVE-2019-19781",
        "Citrix ADC and Gateway path traversal",
        "Citrix ADC and Gateway",
        "2019-12-17",
        "Critical",
        "Citrix",
        "A path-traversal flaw can lead to unauthenticated remote code execution on affected appliances.",
        "Apply Citrix's mitigation and install the corresponding security updates."
    },
    {
        "CVE-2023-23397",
        "Microsoft Outlook Net-NTLM credential leak",
        "Microsoft Outlook",
        "2023-03-14",
        "Critical",
        "Microsoft",
        "A specially crafted message can trigger an authentication attempt and disclose Net-NTLM credentials.",
        "Install the Outlook security update and restrict unnecessary outbound SMB authentication."
    },
    {
        "CVE-2023-4966",
        "Citrix NetScaler sensitive information disclosure",
        "Citrix NetScaler ADC and Gateway",
        "2023-10-10",
        "Critical",
        "Citrix",
        "An affected NetScaler deployment may disclose sensitive information through an out-of-bounds memory read.",
        "Upgrade affected NetScaler builds, terminate active sessions and rotate credentials as advised by Citrix."
    },
    {
        "CVE-2022-22965",
        "Spring4Shell - Spring Framework remote code execution",
        "VMware Spring Framework",
        "2022-04-01",
        "Critical",
        "VMware",
        "A data-binding flaw may allow remote code execution in specific Java deployment configurations.",
        "Upgrade Spring Framework and follow VMware's deployment-specific remediation guidance."
    },
    {
        "CVE-2021-34527",
        "PrintNightmare - Windows Print Spooler remote code execution",
        "Microsoft Windows Print Spooler",
        "2021-07-01",
        "Critical",
        "Microsoft",
        "The Windows Print Spooler contains a remote code execution vulnerability in affected configurations.",
        "Install Microsoft's security updates and disable printing services that are not required."
    },
};

static int contains_ci(const char *text, const char *needle)
{
    size_t n = strlen(needle);

    if(n == 0) return 1;
    while(*text != '\0') {
        size_t i = 0;
        while(i < n && text[i] != '\0' &&
              tolower((unsigned char)text[i]) ==
              tolower((unsigned char)needle[i]))
            i++;
        if(i == n) return 1;
        text++;
    }
    return 0;
}

static int query_tokens(const char *query, char tokens[4][32])
{
    const char *p = query;
    int count = 0;

    if(query == NULL || strlen(query) >= 96) return -1;
    while(*p != '\0') {
        size_t length = 0;
        while(*p == ' ' || *p == '\t') p++;
        if(*p == '\0') break;
        if(count == 4) return -1;
        while(*p != '\0' && *p != ' ' && *p != '\t') {
            if(length + 1 >= 32) return -1;
            tokens[count][length++] = *p++;
        }
        tokens[count][length] = '\0';
        count++;
    }
    return count;
}

static int record_matches(const vqeaf_advisory_t *record,
                          char tokens[4][32], int token_count)
{
    const char *fields[] = {
        record->id, record->title, record->product, record->published,
        record->severity, record->source, record->summary, record->remediation
    };

    for(int i = 0; i < token_count; i++) {
        int found = 0;
        for(size_t j = 0; j < sizeof(fields) / sizeof(fields[0]); j++) {
            if(contains_ci(fields[j], tokens[i])) {
                found = 1;
                break;
            }
        }
        if(!found) return 0;
    }
    return 1;
}

const char *vqeaf_advisory_catalog_revision(void)
{
    return "2026-11-24";
}

size_t vqeaf_advisory_count(void)
{
    return sizeof(catalog) / sizeof(catalog[0]);
}

const vqeaf_advisory_t *vqeaf_advisory_at(size_t index)
{
    return index < vqeaf_advisory_count() ? &catalog[index] : NULL;
}

int vqeaf_advisory_search(const char *query, size_t *out, int max)
{
    char tokens[4][32];
    int token_count;
    int count = 0;

    if(out == NULL || max <= 0) return -1;
    token_count = query_tokens(query, tokens);
    if(token_count < 0) return -1;
    for(size_t i = 0; i < vqeaf_advisory_count() && count < max; i++) {
        if(record_matches(&catalog[i], tokens, token_count))
            out[count++] = i;
    }
    return count;
}
