#include <IOKit/IOLib.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "XBOControllerDriver.h"
#include "XBOControllerHIDReport.h"

OSDefineMetaClassAndStructors(com_anthonyvd_driver_XBOControllerDriver, IOHIDDevice)

#define super IOHIDDevice

#define MAGIC1_SIZE 13
unsigned char magic1[MAGIC1_SIZE] = { 0x05, 0x20, 0x00, 0x09, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x53 };
#define MAGIC2_SIZE 5
unsigned char magic2[MAGIC2_SIZE] = { 0x05, 0x20, 0x01, 0x01, 0x00 };
#define MAGIC3_SIZE 7
unsigned char magic3[MAGIC3_SIZE] = { 0x0a, 0x20, 0x02, 0x03, 0x00, 0x01, 0x14 };
#define MAGIC4_SIZE 13
unsigned char magic4[MAGIC4_SIZE] = { 0x09, 0x00, 0x03, 0x09, 0x00, 0x0f, 0x00, 0x00, 0x1d, 0x1d, 0xff, 0x00, 0x00 };

#define PIPE_TIMEOUT 200

typedef struct async_data_context {
    IOBufferMemoryDescriptor* memory_descriptor;
    IOUSBCompletion callback;
} async_data_context;

int log_iokit_err(IOReturn err, char const* err_str) {
    if(!err) {
        return 0;
    }
    
    printf("%s: %08x %08x %08x\n",
           err_str,
           err_get_system(err),
           err_get_sub(err),
           err_get_code(err));
    return err;
}

void async_data_read(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining) {
    com_anthonyvd_driver_XBOControllerDriver* driver = (com_anthonyvd_driver_XBOControllerDriver*)target;
    async_data_context* ctx = (async_data_context*)parameter;
    
    size_t data_length = ctx->memory_descriptor->getLength();
    
    {
        if(!driver || !ctx) {
            IOLog("[XBOController driver]Error interpreting read callback args\n");
            goto cleanup;
        }
        
        unsigned char* buffer = (unsigned char*)IOMalloc(data_length);
        if(!buffer) {
            IOLog("[XBOController driver]Error allocating read buffer\n");
            goto cleanup;
        }
        
        if(ctx->memory_descriptor->readBytes(0, buffer, data_length) != data_length) {
            IOLog("[XBOController driver]Error reading bytes\n");
            IOFree(buffer, data_length);
            goto cleanup;
        }
        
        driver->async_data_received(buffer, data_length);
        
        IOFree(buffer, data_length);
    }
cleanup:
    ctx->memory_descriptor->complete();
    IOFree(ctx, sizeof(async_data_context));
}

bool com_anthonyvd_driver_XBOControllerDriver::async_data_received(unsigned char* data, size_t length) {
    if(data[0] == 0x20) {
        //This is a state changed event
        unsigned char hat_switch = 0;
        
        if((data[5] & 0x09) == 0x09)
            hat_switch = 2;
        else if((data[5] & 0x0a) == 0x0a)
            hat_switch = 4;
        else if((data[5] & 0x06) == 0x06)
            hat_switch = 6;
        else if((data[5] & 0x05) == 0x05)
            hat_switch = 8;
        else if(data[5] & 0x01)
            hat_switch = 1;
        else if(data[5] & 0x08)
            hat_switch = 3;
        else if(data[5] & 0x02)
            hat_switch = 5;
        else if(data[5] & 0x04)
            hat_switch = 7;
         
        current_state.buttons = (hat_switch & 0x0f) << 12 |
                                (data[4] & 0xf0) >> 4 |
                                (data[5] & 0xf0) |
                                (data[4] & 0x0f) << 8;
        
        current_state.left_trigger = (data[7] << 8) | data[6];
        current_state.right_trigger = (data[9] << 8) | data[8];
        
        current_state.left_stick.x_axis = (data[11] << 8) | data[10];
        current_state.left_stick.y_axis = (data[13] << 8) | data[12];
        
        current_state.right_stick.x_axis = (data[15] << 8) | data[14];
        current_state.right_stick.y_axis = (data[17] << 8) | data[16];
        
        IOBufferMemoryDescriptor* descriptor = IOBufferMemoryDescriptor::inTaskWithOptions(current_task(),
                                                                                           kIODirectionIn | kIODirectionOut,
                                                                                           sizeof(XBOControllerState));
        
        descriptor->writeBytes(0, &current_state, sizeof(XBOControllerState));
        IOReturn ret = handleReport(descriptor);
        log_iokit_err(ret, "[XBOController driver]Error making handleReport data");
    }
    return setup_async_read();
}

bool com_anthonyvd_driver_XBOControllerDriver::write_data(void* buffer, size_t size) {
    IOBufferMemoryDescriptor* descriptor = IOBufferMemoryDescriptor::inTaskWithOptions(current_task(),
                                                                                       kIODirectionIn | kIODirectionOut,
                                                                                       size);
    if(!descriptor) {
        IOLog("[XBOController driver]Error creating descriptor\n");
        return false;
    }
    
    IOReturn prep = descriptor->prepare();
    if(descriptor->writeBytes(0, buffer, size) != size) {
        IOLog("[XBOController driver]Error writing bytes\n");
        return false;
    }
    
    IOReturn ret = out_pipe->Write(descriptor);
    IOReturn comp = descriptor->complete();
    if(log_iokit_err(prep, "[XBOController driver]Error preparing memory buffer") ||
       log_iokit_err(ret, "[XBOController driver]Error writing data to out_pipe") ||
       log_iokit_err(comp, "[XBOController driver]Error completing memory buffer")) {
        
        return false;
    }
    return true;
}

bool com_anthonyvd_driver_XBOControllerDriver::send_magic_values() {
    
    IOLog("[XBOController driver]Writing magic 1\n");
    if(!write_data(magic1, MAGIC1_SIZE)) {
        return false;
    }
    IOLog("[XBOController driver]Done writing magic 1\n");
    
    IOLog("[XBOController driver]Writing magic 2\n");
    if(!write_data(magic2, MAGIC2_SIZE)) {
        return false;
    }
    IOLog("[XBOController driver]Done writing magic 2\n");
    
    IOLog("[XBOController driver]Writing magic 3\n");
    if(!write_data(magic3, MAGIC3_SIZE)) {
        return false;
    }
    IOLog("[XBOController driver]Done writing magic 3\n");
    
    IOLog("[XBOController driver]Writing magic 4\n");
    if(!write_data(magic4, MAGIC4_SIZE)) {
        return false;
    }
    IOLog("[XBOController driver]Done writing magic 4\n");
    
    return true;
}

bool com_anthonyvd_driver_XBOControllerDriver::setup_async_read() {
    if(terminated) {
        return true;
    }
    
    IOLog("[XBOController driver]Setting up read async\n");
    IOBufferMemoryDescriptor* descriptor = IOBufferMemoryDescriptor::inTaskWithOptions(current_task(),
                                                                                       kIODirectionIn | kIODirectionOut,
                                                                                       64);
    
    async_data_context* ctx = (async_data_context*)IOMalloc(sizeof(async_data_context));
    ctx->callback.target = this;
    ctx->callback.parameter = ctx;
    ctx->callback.action = async_data_read;
    ctx->memory_descriptor = descriptor;

    IOReturn prep = descriptor->prepare();
    IOReturn ret = in_pipe->Read(descriptor, &ctx->callback);
    
    if(log_iokit_err(prep, "[XBOController driver]Error preparing memory descriptor") ||
       log_iokit_err(ret, "[XBOController driver]Error in Read() call")) {
        
        descriptor->complete();
        IOFree(ctx, sizeof(async_data_context));
        return false;
    }
       
    IOLog("[XBOController driver]Done setting up read async\n");
    return true;
}

bool com_anthonyvd_driver_XBOControllerDriver::init(OSDictionary* dict) {
    bool result = super::init(dict);
    IOLog("[XBOController driver]Initializing\n");
    attached_interface = NULL;
    in_pipe = NULL;
    out_pipe = NULL;
    terminated = false;
    return result;
}

void com_anthonyvd_driver_XBOControllerDriver::free(void) {
    IOLog("[XBOController driver]Freeing\n");
    super::free();
}

IOService* com_anthonyvd_driver_XBOControllerDriver::probe(IOService* provider, SInt32* score) {
    super::probe(provider, score);
    IOLog("[XBOController driver]Probing\n");
    IOUSBDevice* device = (IOUSBDevice*)provider;
    bool suitable =
        device->GetVendorID()   == 0x045E &&
        device->GetProductID()  == 0x02D1;
    
    IOService* ret = NULL;
    if(suitable) {
        *score = 90000;
        ret = this;
        IOLog("[XBOController driver]Device is suitable ");
    } else {
        IOLog("[XBOController driver]Device is not suitable ");
    }
    
    IOLog("[XBOController driver]vendor ID: %d, product ID: %d, score %d\n",
          device->GetVendorID(),
          device->GetProductID(),
          *score);
    return ret;
}

bool com_anthonyvd_driver_XBOControllerDriver::start(IOService* provider) {
    bool result = super::start(provider);
    IOLog("[XBOController driver]Starting\n");
    device = (IOUSBDevice*)provider;
    
    if(!device->open(this)) {
        IOLog("[XBOController driver]Error opening device\n");
        super::stop(provider);
        return false;
    }
    
    //As of 22-07-2014 the controller only has one configuration available
    IOReturn ret = device->SetConfiguration(this, 1, false);
    if(log_iokit_err(ret, "[XBOController driver]Error setting configuration")) {
        device->close(this);
        super::stop(provider);
        return false;
    }
    
    //We know the interface is going to be the first one because reasons
    IOUSBFindInterfaceRequest request;
    request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    
    if((attached_interface = device->FindNextInterface(attached_interface, &request))) {
        if(!attached_interface->open(this)) {
            IOLog("[XBOController driver]Error opening interface\n");
            device->close(this);
            super::stop(provider);
            return false;
        }
        
        UInt8 num_endpoints = attached_interface->GetNumEndpoints();
        if(num_endpoints != 2) {
            IOLog("[XBOController driver]Expected 2 endpoints, got %d\n", num_endpoints);
            attached_interface->close(this);
            device->close(this);
            super::stop(provider);
            return false;
        }
        
        IOUSBFindEndpointRequest endpoint_request;
        endpoint_request.type = kUSBInterrupt;
        endpoint_request.direction = kUSBIn;
        endpoint_request.maxPacketSize = 64;
        endpoint_request.interval = 4;
        
        if(!(in_pipe = attached_interface->FindNextPipe(in_pipe, &endpoint_request))) {
            IOLog("[XBOController driver]Error setting in_pipe\n");
            attached_interface->close(this);
            device->close(this);
            super::stop(provider);
            return false;
        }
        
        endpoint_request.direction = kUSBOut;
        if(!(out_pipe = attached_interface->FindNextPipe(out_pipe, &endpoint_request))) {
            IOLog("[XBOController driver]Error setting out_pipe");
            attached_interface->close(this);
            device->close(this);
            super::stop(provider);
            return false;
        }
        
        //Send magic values
        if(!send_magic_values()) {
            attached_interface->close(this);
            device->close(this);
            super::stop(provider);
            return false;
        }
        
        //Setup reads to interpret interrupts
        if(!setup_async_read()) {
            attached_interface->close(this);
            device->close(this);
            super::stop(provider);
            return false;
        }

        out_pipe->retain();
        in_pipe->retain();
        return true;
    } else {
        IOLog("[XBOController driver]Error finding interface\n");
        device->close(this);
        super::stop(provider);
        return false;
    }
}

IOReturn com_anthonyvd_driver_XBOControllerDriver::message(UInt32 type, IOService* provider, void* argument) {
    super::message(type, provider, argument);
    IOLog("[XBOController driver]Message\n");
    if(type == kIOMessageServiceIsTerminated) {
        IOLog("[XBOController driver]Message was a terminate\n");
        terminated = true;
        willTerminate(provider, 0);
        destroy();
        terminate();
        bool defer = false;
        didTerminate(provider, 0, &defer);
        messageClients(kIOMessageServiceIsTerminated);
    }
    
    return kIOReturnSuccess;
}

bool com_anthonyvd_driver_XBOControllerDriver::requestTerminate(IOService* provider, IOOptionBits options) {
    IOLog("[XBOController driver]Requesting terminate\n");
    return true;
}

void com_anthonyvd_driver_XBOControllerDriver::stop(IOService* provider) {
    IOLog("[XBOController driver]Stopping\n");
    if(!terminated) {
        destroy();
    }
    super::stop(provider);
}

void com_anthonyvd_driver_XBOControllerDriver::destroy() {
    IOLog("[XBOController driver]Destroying\n");
    out_pipe->Abort();
    out_pipe->release();
    in_pipe->Abort();
    in_pipe->release();
    attached_interface->close(this);
    device->close(this);
    IOLog("[XBOController driver]Done destroying\n");
}

IOReturn com_anthonyvd_driver_XBOControllerDriver::newReportDescriptor(IOMemoryDescriptor** descriptor ) const {
    IOLog("[XBOController driver]Requesting report descriptor\n");
    *descriptor = IOBufferMemoryDescriptor::inTaskWithOptions(current_task(),
                                                              kIODirectionIn | kIODirectionOut,
                                                              sizeof(XBOControllerHIDReport));
    
    (*descriptor)->writeBytes(0, &XBOControllerHIDReport, sizeof(XBOControllerHIDReport));
    return kIOReturnSuccess;
}
