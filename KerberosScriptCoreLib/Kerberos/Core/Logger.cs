namespace Kerberos.Source.Kerberos.Core
{
    internal static class Logger
    {
        public static void Log(string message, params object[] args)
        {
            InternalCalls.NativeLog(message);
        }

        public static void Log(string message)
        {
            InternalCalls.NativeLog(message);
        }
    }
}