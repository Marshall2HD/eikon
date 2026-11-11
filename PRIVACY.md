# Lucida privacy policy

Lucida is a screenshot tool for Windows 11. It runs on your PC and has no servers of its own.

## What Lucida collects

Nothing. Lucida has no analytics, no telemetry, no crash reporting, no accounts and no update check.
The developer receives no data from Lucida.

## What stays on your device

Screenshots, the screenshots folder, your settings and your hotkeys stay on your PC. Lucida's library
reads the screenshots folder you choose in Settings and never sends it anywhere.

## Uploads

Lucida uploads an image only when you use the upload feature and configure an S3-compatible bucket.
The image goes to the endpoint and bucket you entered, and nowhere else. The access key and secret you
enter are encrypted with Windows' data protection for your Windows account (DPAPI) and stored only on
your PC. "Test Connection" sends a request to your bucket and nothing else. A sharing link is made on
your PC, either as a signed link or from the public URL base you set.

## Processing on your device

Cut Out runs on your PC. It uses Windows' own on-device AI where your PC has it, or a model included in
Lucida. Text and QR code recognition use on-device tools too. On a PC where Windows' AI model is not
ready yet, Lucida asks Windows to prepare it. That is Windows setting up its own component, not Lucida
sending your data.

## Contact

Questions or concerns: open an issue at https://github.com/Marshall2HD/lucida/issues.
