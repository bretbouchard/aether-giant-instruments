//
//  ViewController.swift
//  GiantInstrumentsPluginApp
//
//  Main view controller for the host app
//

import UIKit

class ViewController: UIViewController {

    override func viewDidLoad() {
        super.viewDidLoad()

        view.backgroundColor = .systemBackground

        // Create title label
        let titleLabel = UILabel()
        titleLabel.text = "Giant Instruments"
        titleLabel.font = .systemFont(ofSize: 32, weight: .bold)
        titleLabel.textAlignment = .center
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(titleLabel)

        // Create subtitle label
        let subtitleLabel = UILabel()
        subtitleLabel.text = "AUv3 Instrument Extension"
        subtitleLabel.font = .systemFont(ofSize: 18, weight: .regular)
        subtitleLabel.textColor = .secondaryLabel
        subtitleLabel.textAlignment = .center
        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(subtitleLabel)

        // Create info label
        let infoLabel = UILabel()
        infoLabel.text = "This app contains the Giant Instruments AUv3 extension.\n\nOpen GarageBand, Logic Pro, or any AUv3-compatible host to use the plugin."
        infoLabel.font = .systemFont(ofSize: 16)
        infoLabel.textColor = .label
        infoLabel.textAlignment = .center
        infoLabel.numberOfLines = 0
        infoLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(infoLabel)

        // Create instrument list
        let instrumentsLabel = UILabel()
        instrumentsLabel.text = "Instruments Included:\n• Giant Strings\n• Giant Drums\n• Giant Voice\n• Giant Horns\n• Giant Percussion"
        instrumentsLabel.font = .systemFont(ofSize: 14)
        instrumentsLabel.textColor = .secondaryLabel
        instrumentsLabel.textAlignment = .left
        instrumentsLabel.numberOfLines = 0
        instrumentsLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(instrumentsLabel)

        // Setup constraints
        NSLayoutConstraint.activate([
            // Title label
            titleLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            titleLabel.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 40),

            // Subtitle label
            subtitleLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 10),

            // Info label
            infoLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            infoLabel.topAnchor.constraint(equalTo: subtitleLabel.bottomAnchor, constant: 40),
            infoLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 40),
            infoLabel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -40),

            // Instruments label
            instrumentsLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            instrumentsLabel.topAnchor.constraint(equalTo: infoLabel.bottomAnchor, constant: 40),
            instrumentsLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 60),
            instrumentsLabel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -60)
        ])
    }
}
