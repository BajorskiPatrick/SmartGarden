import { Sprout, Wifi, Smartphone, Check, ArrowRight } from 'lucide-react';
import { Link } from 'react-router-dom';

export function ProvisionPage() {
  return (
    <div className="max-w-4xl mx-auto py-8 px-4">
      <div className="text-center mb-12">
        <h1 className="text-3xl font-bold text-gray-900 dark:text-white mb-4">
          Add New Device
        </h1>
        <p className="text-lg text-gray-600 dark:text-gray-400">
          Follow these steps to connect your Smart Garden device to your account.
        </p>
      </div>

      <div className="grid gap-8 md:grid-cols-2 lg:grid-cols-3">
        {/* Step 1 */}
        <div className="bg-white dark:bg-gray-800 p-6 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
          <div className="w-12 h-12 bg-green-100 dark:bg-green-900/30 rounded-full flex items-center justify-center mb-4 text-green-600">
            <Wifi className="w-6 h-6" />
          </div>
          <h3 className="text-xl font-semibold mb-2 text-gray-900 dark:text-white">1. Power On</h3>
          <p className="text-gray-600 dark:text-gray-400 mb-4">
            Plug in your Smart Garden device. The LED indicator should start blinking blue, indicating it's in provisioning mode.
          </p>
        </div>

        {/* Step 2 */}
        <div className="bg-white dark:bg-gray-800 p-6 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
          <div className="w-12 h-12 bg-blue-100 dark:bg-blue-900/30 rounded-full flex items-center justify-center mb-4 text-blue-600">
            <Smartphone className="w-6 h-6" />
          </div>
          <h3 className="text-xl font-semibold mb-2 text-gray-900 dark:text-white">2. Connect WiFi</h3>
          <p className="text-gray-600 dark:text-gray-400 mb-4">
            Connect to the WiFi network named <strong>SmartGarden-Prov</strong> using your phone or laptop.
          </p>
        </div>

        {/* Step 3 */}
        <div className="bg-white dark:bg-gray-800 p-6 rounded-xl shadow-sm border border-gray-200 dark:border-gray-700">
          <div className="w-12 h-12 bg-purple-100 dark:bg-purple-900/30 rounded-full flex items-center justify-center mb-4 text-purple-600">
             <Check className="w-6 h-6" />
          </div>
          <h3 className="text-xl font-semibold mb-2 text-gray-900 dark:text-white">3. Complete Setup</h3>
          <p className="text-gray-600 dark:text-gray-400 mb-4">
            A configuration page should open automatically. Enter your home WiFi details and your current username:
            <br/>
            <strong className="block mt-2 bg-gray-100 dark:bg-gray-900 p-1 rounded text-center">
               {/* Note: In a real app we would get this from Context */}
               Your Username
            </strong>
          </p>
        </div>
      </div>

      <div className="mt-12 text-center">
        <div className="inline-flex items-center gap-2 p-4 bg-yellow-50 dark:bg-yellow-900/20 text-yellow-800 dark:text-yellow-200 rounded-lg max-w-2xl text-left">
           <Sprout className="w-12 h-12 flex-shrink-0" />
           <div>
             <p className="font-semibold">Development Note:</p>
             <p className="text-sm">
               Currently, provisioning is handled by the ESP32 via a captive portal. Ensure your device is flashed with the latest firmware in the `esp32` directory.
             </p>
           </div>
        </div>
      </div>
       
      <div className="mt-8 text-center">
        <Link to="/" className="text-green-600 hover:text-green-700 font-medium inline-flex items-center gap-2">
          Back to Dashboard <ArrowRight className="w-4 h-4" />
        </Link>
      </div>
    </div>
  );
}
