#include <iostream>
#include <memory>
#include <Windows.h>

class JobObject
{
private:
    struct de_hJobObject
    {
        void operator()(HANDLE hJobObject) const noexcept
        {
            CloseHandle(hJobObject);
        }
    };
    using unique_ptr_hjo = std::unique_ptr<void,de_hJobObject>;

public:
    HANDLE creative_jo()
    {
        unique_ptr_hjo hjobobject(
            CreateJobObjectW(
                nullptr,
                nullptr
        ));
        AssignProcessToJobObject(
            hjobobject.get(),
            GetCurrentProcess()
        );
        
        return hjobobject.get();
    }
       
};

int main()
{
    JobObject jo;
    std::cout << jo.creative_jo();
    return 0;
}