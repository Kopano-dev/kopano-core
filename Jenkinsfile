#!/usr/bin/env groovy

pipeline {
    agent {
        dockerfile {
            filename 'Dockerfile.build'
        }
    }
    stages {
        stage('Build') {
            steps {
                echo 'Building..'
                sh './bootstrap.sh'
                sh 'PYTHON=/usr/bin/python3 ./configure'
                sh 'make -j $(nproc)'
            }
        }
        stage('Lint') {
            steps {
                echo 'Linting..'
                sh 'pylint3 swig/python/kopano/kopano > pylint.log || true'
                recordIssues tool: pyLint(pattern: 'pylint.log'), qualityGates: [[threshold: 1, type: 'TOTAL', unstable: true]]
                archiveArtifacts 'pylint.log'
            }
        }
        stage('Check') {
            steps {
                echo 'Checking..'
                sh 'make check'
            }
        }
        stage('Test') {
            steps {
                echo 'Testing php-ext..'
                sh 'make test TEST_PHP_JUNIT=test.log || true'
		junit allowEmptyResults: true, testResults: 'php-ext/test.log'
            }
        }

        stage('Python Test') {
            steps {
                echo 'Testing python-mapi..'
                sh 'make -C swig/python test PYTEST=pytest-3 || true'
                junit allowEmptyResults: true, testResults: 'swig/python/test.xml'
            }
        }
        stage('Python Kopano Test') {
            steps {
                echo 'Testing python-kopano..'
                sh 'make -C swig/python/kopano test PYTEST=pytest-3 || true'
                junit testResults: 'swig/python/kopano/test.xml'
            }
        }
    }
    post {
        always {
            cleanWs()
        }
    }
}
