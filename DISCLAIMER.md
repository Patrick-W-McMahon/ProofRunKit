# Disclaimer

This notice is an additional statement of the author's intent. It does not modify, replace, or limit the
[Apache License, Version 2.0](LICENSE), including section 7 (Disclaimer of Warranty) and section 8 (Limitation of
Liability). If anything here appears to conflict with the license, the license governs. This notice is not legal
advice.

## No warranty

THE SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied, including, without limitation, any warranties or conditions of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or
FITNESS FOR A PARTICULAR PURPOSE. You use ProofRunKit at your own risk.

## Limitation of liability

To the fullest extent permitted by applicable law, the author, copyright holders, and contributors are not liable for
any damages arising from use of, or inability to use, the software. This includes, without limitation, direct,
indirect, incidental, special, consequential, or punitive damages, and loss of data, profits, goodwill, or business,
whether in an action of contract, tort (including negligence), or otherwise, even if advised of the possibility of
such damages.

## ProofRunKit runs code you supply

ProofRunKit compiles, launches, and otherwise executes compilers, assemblers, interpreters, scripts, tools, and
binaries that you supply or that your tests invoke. The kit does not review or vet that code. You are solely
responsible for those inputs and for any effects of running them, including effects on your machines, files,
networks, accounts, data, and third-party systems. Run untrusted tests only in an environment you are prepared to
lose, such as a disposable virtual machine.

The embedded script sandbox (see [#9](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/9) and
[#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13)) limits what a test script can reach directly. It is
a safety aid, not a security boundary, and it does not constrain the external programs a test starts.

## Data loss

Test runs can create, overwrite, or delete files, for example compiler outputs and artifact folders. Keep backups and
commit your work before running tests. The author is not responsible for lost or damaged data.

## Antivirus and SmartScreen

ProofRunKit is built to stay a stable, rarely changing binary so that antivirus products have fewer reasons to flag it
(see [docs/stable-binary.md](docs/stable-binary.md)). Installers are currently unsigned. Antivirus software, Windows
SmartScreen, or similar tools may still report ProofRunKit, its installers, or programs it builds as unknown or
suspicious (a false positive). Any exclusion or allow-list decision is yours; the author makes no guarantee about how
third-party security software treats the kit.
