#include <functional>
#import <Foundation/Foundation.h>
namespace trussc::internal {
void resourceLoadAutoreleasePool(const std::function<void()>& work) {
    @autoreleasepool { work(); }
}
}
