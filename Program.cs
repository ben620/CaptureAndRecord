using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.AccessControl;

namespace FFVideo
{
    static class FnFrameRecorder
    {
        const string dllPath = "VideoEncode.dll";
        [DllImport(dllPath, EntryPoint = "CreateInit", CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr CreateInit([MarshalAs(UnmanagedType.LPStr)] string name, int fps, int width, int height);
        [DllImport(dllPath, EntryPoint = "RecordFrame", CallingConvention = CallingConvention.Cdecl)]
        public static extern void RecordFrame(UIntPtr inst);
        [DllImport(dllPath, EntryPoint = "Stop", CallingConvention = CallingConvention.Cdecl)]
        public static extern void Stop(UIntPtr inst);
    }

    public class FrameRecorder
    {
        UIntPtr self = UIntPtr.Zero;
        public bool NewRecord(string name, int fps, int width, int height)
        {
            self = FnFrameRecorder.CreateInit(name, fps, width, height);
            return self == UIntPtr.Zero;
        }

        public void RecordFrame()
        {
            FnFrameRecorder.RecordFrame(self);
        }

        public void Stop()
        {
            FnFrameRecorder.Stop(self);
            self = UIntPtr.Zero;
        }
    }
}



namespace CSharpTest
{
    internal class Program
    {
        static void Main(string[] args)
        {
            var r = new FFVideo.FrameRecorder();
            int width = 1920;
            int height = 1080;

            r.NewRecord(".\\test.mp4", 30, width, height);
            TimeSpan frameTime = new TimeSpan(0, 0, 0, 0, 33);
            var lastTime = DateTime.MinValue;
            for (int frameID = 0; frameID < 180;)
            {
                var beginTime = DateTime.UtcNow;
                if (beginTime - lastTime >= frameTime)
                {
                    Console.Write(string.Format("{0} {1}\n", beginTime.Millisecond, (beginTime - lastTime).TotalMilliseconds));
                    lastTime = beginTime;
                    r.RecordFrame();
                    ++frameID;
                }

                //magic
                Thread.Sleep(0);
                Thread.Sleep(0);
            }

            r.Stop();
        }
    }
}