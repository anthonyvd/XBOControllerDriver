XBOControllerDriver
===================

A Mac OS X kernel extension to support the Xbox One controller.

It's not ready from prime time so the only way to install it at the moment is to build it yourself and load it with kextutil.  I don't recommend this.  At all.  It's a terrible idea.

Newer versions of OS X require KEXTs to be signed.  I'll look into making an installable package in the near future and acquiring a certificate to sign the kext.

TODO:
* Properly release the driver when the controller is unplugged
* Remap the buttons so they have a more sensible order
* Enable rumble/responsive triggers
* Create an installer
