//
//  AppDelegate.swift
//  GiantInstrumentsPluginApp
//
//  Host app for Giant Instruments AUv3 extension
//

import UIKit

@main
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {

        // Create window
        window = UIWindow(frame: UIScreen.main.bounds)

        // Create root view controller
        let viewController = ViewController()
        window?.rootViewController = viewController
        window?.makeKeyAndVisible()

        return true
    }
}
