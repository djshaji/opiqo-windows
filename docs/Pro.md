# Demo Version Limitations
- Restrict use of plugin slot 2,3 and 4
- When use tries to add a plugin to those slots, show Upgrade dialog
- Disable export format dropdown
- Show "Demo Version" in title bar

# Pro Version Changes
- Remove all demo version limitations
- Remove "Demo Version" from title bar
- Add "Pro Version" to title bar
- Add "Pro Version" to about dialog

# Upgrade Dialog
- Title: "Upgrade to Pro Version"
- Message: "Unlock all features and support development by upgrading to the Pro Version."
- Buttons: "Upgrade Now" (opens purchase page), "Cancel" (closes dialog
- Add text input with label "Enter License Key" and "Activate" button for users who have already purchased
- Validate license key format (e.g. regex) and show error message if invalid
- Simple totp check using src/win32/totp.h verifyTotp() 
- Save license key and activation status in AppSettings for persistence
- On app startup, check activation status and enable Pro features if valid license key is present