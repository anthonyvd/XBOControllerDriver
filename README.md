XBOControllerDriver
===================

A Mac OS X kernel extension to support the Xbox One controller.

It's not ready from prime time so the only way to install it at the moment is to build it yourself and load it with kextutil.  I don't recommend this.  At all.  It's a terrible idea.

Newer versions of OS X require KEXTs to be signed.  I'll look into making an installable package in the near future and acquiring a certificate to sign the kext.

Interested in what went into making this?  There's a write-up here: http://definedbehavior.com/reverse-engineering-the-xbox-one-controller/

TODO:
* Properly release the driver when the controller is unplugged
* Enable rumble/responsive triggers
* Create an installer
