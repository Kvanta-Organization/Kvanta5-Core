# Security Policy

Security is a critical priority for Kvanta5. Responsible disclosure helps protect the network, its users, infrastructure operators, miners, exchanges, wallet providers, and other ecosystem participants.

## Supported Versions

Security updates are provided for the latest officially released version of Kvanta5 Core.

Older releases may no longer receive security fixes. Users and infrastructure operators should keep their software updated and verify releases through official Kvanta5 distribution channels.

## Reporting a Vulnerability

To privately report a suspected security vulnerability, email:

**[security@kvanta5.org](mailto:security@kvanta5.org)**

This address is for security reports only and is not intended for general support, configuration assistance, mining issues, exchange inquiries, or feature requests.

Please do not publicly disclose the vulnerability, open a public GitHub issue, or discuss the issue in public channels before the Kvanta5 developers have had a reasonable opportunity to investigate and address it.

## What to Include

Please include as much relevant information as possible:

* A clear description of the vulnerability.
* The affected software, component, version, or commit.
* Steps required to reproduce the issue.
* Any proof-of-concept code, logs, stack traces, or screenshots.
* The potential impact on users, nodes, wallets, miners, services, or network consensus.
* Any conditions required for exploitation.
* Whether the issue has already been disclosed to anyone else.
* Your preferred name or attribution, if applicable.

Reports that are clear, reproducible, and technically detailed can be investigated more efficiently.

## Vulnerability Scope

Examples of security issues that should be reported privately include:

* Consensus failures or chain-split conditions.
* Unauthorized creation, destruction, duplication, or theft of funds.
* Signature verification or cryptographic validation failures.
* Transaction, block, script, or address validation bypasses.
* Remote code execution.
* Memory corruption or exploitable crashes.
* Network-level attacks that could significantly disrupt node operation.
* Wallet vulnerabilities that could expose private keys or permit unauthorized transactions.
* Authentication, authorization, or privilege-escalation vulnerabilities.
* Build, release, update, or dependency-chain compromises.
* Vulnerabilities affecting official Kvanta5 infrastructure or services.

The following are generally not considered security vulnerabilities:

* General support requests.
* Feature requests or usability concerns.
* Expected blockchain behavior.
* Issues requiring physical access to a user's machine.
* Attacks requiring a user to install untrusted software or disclose private keys.
* Reports based only on automated scanner output without a demonstrated security impact.
* Denial-of-service reports with no meaningful impact beyond ordinary resource consumption.
* Social engineering, impersonation, fraudulent token listings, or scams conducted by unrelated third parties.
* Vulnerabilities affecting unofficial forks, third-party software, exchanges, mining pools, or services not operated by the Kvanta5 project.

Third-party vulnerabilities should be reported directly to the affected operator or software maintainer.

## Disclosure Process

After receiving a report, the Kvanta5 developers will make reasonable efforts to:

1. Review and acknowledge the report.
2. Confirm whether the issue is reproducible and within scope.
3. Assess its severity and potential network impact.
4. Develop and test an appropriate mitigation or fix.
5. Coordinate release and disclosure when necessary.

Complex vulnerabilities, particularly those involving consensus behavior, cryptography, wallet security, or coordinated network upgrades, may require additional investigation and testing before details can be safely disclosed.

The reporter may be asked to keep technical details confidential until affected users and infrastructure operators have had a reasonable opportunity to update.

## Coordinated Disclosure

Please allow the Kvanta5 developers sufficient time to investigate and remediate a vulnerability before publishing technical details.

Public disclosure should be coordinated so that fixes, mitigations, and upgrade instructions can be made available before exploitation becomes practical.

Kvanta5 may withhold certain technical details temporarily when immediate disclosure could place the network or its users at substantial risk.

## Safe Harbor

Kvanta5 supports good-faith security research intended to identify and responsibly disclose vulnerabilities.

Researchers must not:

* Steal, destroy, alter, or intentionally expose user funds or private information.
* Disrupt the Kvanta5 network or third-party infrastructure.
* Access data or systems beyond what is necessary to demonstrate the vulnerability.
* Exploit a vulnerability for financial gain.
* Demand payment, compensation, or other consideration as a condition of disclosure.
* Publicly disclose an unresolved vulnerability without reasonable coordination.

This policy does not authorize activity that violates applicable law or the rights of third parties.

## Bug Bounties

Kvanta5 does not currently operate a guaranteed bug bounty program.

Submission of a vulnerability report does not create an entitlement to payment, compensation, employment, or public attribution. Any recognition or reward is entirely discretionary.

## Security Updates

Security notices, software releases, and upgrade instructions will be published through official Kvanta5 communication and distribution channels when appropriate.

Users should independently verify downloaded software, release information, repository locations, and announcements before installing or executing software.
